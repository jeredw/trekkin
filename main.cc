#include <fcntl.h>
#include <inttypes.h>
#include <linux/joystick.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>

#include <uv.h>
#include "picojson/picojson.h"

#include "audio.h"
#include "game.h"
#include "log.h"
#include "misc.h"
#include "scores.h"

using namespace trek;

#define CLIENT(x) ((Client *)(((uv_handle_t *)x)->data))
#define PANEL(x) (CLIENT(x)->panel)
#define DLOG(client, ...) \
  if (verbose) log(client->name.c_str(), __VA_ARGS__)
#define CLOG(client, ...) log(client->name.c_str(), __VA_ARGS__)
#define GLOG(...) log("G", __VA_ARGS__)

const uint64_t KEEPALIVE_MSEC = 5000;           // send app level keepalives
const unsigned int ZOMBIE_SLAYER_MSEC = 10000;  // drop panels that don't ack

static const char *server_ip = "0.0.0.0";
static int server_port = 8000;
static bool verbose = false;
static const char *gamepad_device = "/dev/input/js0";
bool sfx = false;

static Game G;             // global mc globalface
static int now = 0;        // current tick of game loop
static uv_pipe_t display;  // send updates to the display

// status shown after ready but waiting to join game for a while
static const char *const EXCUSES[] = {
    "(balancing checkbook...)",   "(checking wiper fluid...)",
    "(defragging hard drive...)", "(deicing ailerons...)",
    "(grabbing a coffee...)",     "(shampooing carpets...)",
    "(warming up engine...)",
};

// status shown after a successful command completion
static const char *const GOOD_MESSAGES[] = {
    "nice move!", "acknowledged!", "make it so!", "you go girl!",
    "awwright!",  "keep it up!",   "yeaah boy!",
};

// shown after a failed command times out
static const char *const BAD_MESSAGES[] = {
    "baloney!",
    "bunkum!",
    "egad!",
    "hogwash!", "hokum!",
    "malarkey!",
    "poppycock!",
    "phooey!",
};

// set of valid characters for player initials
static const char *const INITIALS = "?ABCDEFGHIJKLMNOPQRSTUVWXYZ";

struct MissionTiming {
  int timeout;
  int rest;
  int command_limit;
};

static const MissionTiming MISSION_TIMING[] = {
    // timeout           rest                command limit
    {ms_to_ticks(20000), ms_to_ticks(5000), 10},
    {ms_to_ticks(20000), ms_to_ticks(5000), 15},
    {ms_to_ticks(15000), ms_to_ticks(5000), 20},
    {ms_to_ticks(10000), ms_to_ticks(0), 25},
    {ms_to_ticks(5000), ms_to_ticks(0), 30},
};

static const int KONAMI_CODE[] = {
    DPAD_UP,   0, DPAD_UP,    0, DPAD_DOWN,    0, DPAD_DOWN,  0,
    DPAD_LEFT, 0, DPAD_RIGHT, 0, DPAD_LEFT,    0, DPAD_RIGHT, 0,
    B_BUTTON,  0, A_BUTTON,   0, START_BUTTON,
};

static uv_poll_t gamepad;

static void usage() {
  fprintf(stderr,
          "Usage: trekkin [-ip IP] [-port PORT] [-gamepad DEVICE] [-verbose] "
          "[-sfx]\n");
}

static void parse_command_line(int argc, char **argv) {
  for (argc--, argv++; argc > 0; argc--, argv++) {
    std::string arg = argv[0];
    if (arg == "-ip" && argc > 1) {
      server_ip = *++argv;
      argc--;
    } else if (arg == "-port" && argc > 1) {
      server_port = atoi(*++argv);
      argc--;
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (arg == "-sfx") {
      sfx = true;
    } else if (arg == "-gamepad" && argc > 1) {
      gamepad_device = *++argv;
      argc--;
    } else {
      fprintf(stderr, "unrecognized option: %s\n", argv[0]);
      usage();
      exit(1);
    }
  }
}

template <typename Predicate>
static void remove_command_if(Predicate pred) {
  auto &commands = G.commands;
  commands.erase(std::remove_if(commands.begin(), commands.end(), pred),
                 commands.end());
}

static void process_client_message(uv_handle_t *handle) {
  Client *client = CLIENT(handle);
  Panel *panel = &client->panel;
  auto *controls = &panel->controls;
  try {
    picojson::value v;
    std::string parse_error = picojson::parse(v, client->pending_message);
    if (!parse_error.empty()) {
      DLOG(client, "parse error: %s", parse_error.c_str());
      return;
    }
    const auto &message = v.get("message").get<std::string>();
    if (message == "announce") {
      const auto &announced_controls =
          v.get("data").get("controls").get<picojson::array>();
      controls->clear();
      for (const auto &announced : announced_controls) {
        Control control;
        control.id = announced.get("id").get<std::string>();
        control.state = announced.get("state").get<std::string>();
        const auto &actions = announced.get("actions").get<picojson::object>();
        DLOG(client, "+ control %s [state %s]", control.id.c_str(),
             control.state.c_str());
        for (const auto &action : actions) {
          const std::string &state = action.first;
          const std::string &desc = action.second.get<std::string>();
          if (desc.empty()) {
            DLOG(client, "|-- (skipped because empty desc) %s", state.c_str());
            continue;
          }
          control.actions[state] = desc;
          DLOG(client, "|-- action %s %s", state.c_str(), desc.c_str());
        }
        controls->push_back(control);
      }
      // assume any commands for the panel are now invalid
      remove_command_if(
          [handle](const Command &command) { return command.doer == handle; });
    } else if (message == "set-state") {
      const auto &id = v.get("data").get("id").get<std::string>();
      const auto &state = v.get("data").get("state").get<std::string>();
      auto control = std::find_if(controls->begin(), controls->end(),
                                  [id](Control &c) { return c.id == id; });
      if (control != controls->end()) {
        if (control->state != state) {
          panel->last_state_change_tick = now;
        }
        control->state = state;
        for (auto &command : G.commands) {
          if (command.doer == handle && command.id == id &&
              command.desired_state == state) {
            // so the game timer doesn't miss it if it toggles back off
            command.done = true;
          }
        }
        DLOG(client, "set control %s to %s", id.c_str(), state.c_str());
      } else {
        DLOG(client, "unknown control %s", id.c_str());
      }
    }
  } catch (...) {
    DLOG(client, "invalid message json");
  }
}

static void remove_associated_commands(uv_handle_t *handle) {
  remove_command_if([handle](const Command &command) {
    return command.doer == handle || command.shower == handle;
  });
}

static uv_tcp_t *init_client_handle() {
  uv_tcp_t *tcp = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
  Client *client = new Client;
  tcp->data = client;
  G.handles.push_back((uv_handle_t *)tcp);
  return tcp;
}

static void cleanup_client_handle(uv_handle_t *handle) {
  remove_associated_commands(handle);
  G.handles.erase(std::remove(G.handles.begin(), G.handles.end(), handle),
                  G.handles.end());
  delete CLIENT(handle);
  free(handle);
}

static void on_recv(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  Client *client = CLIENT(stream);
  if (nread < 0) {
    CLOG(client, "disconnected: %s", uv_strerror(nread));
    uv_close((uv_handle_t *)stream, cleanup_client_handle);
    return;
  }
  for (int i = 0; nread > 0;) {
    if (client->got_length_bytes != 4) {
      nread--;
      client->want_message_bytes =
          (client->want_message_bytes << 8) | buf->base[i++];
      client->got_length_bytes++;
      if (client->got_length_bytes == 4) {
        if (client->want_message_bytes < 0 ||
            client->want_message_bytes > 150000) {
          CLOG(client, "reset due to %d byte message",
               client->want_message_bytes);
          uv_close((uv_handle_t *)stream, cleanup_client_handle);
          return;
        }
        client->pending_message = std::string();
      }
    } else if (nread < client->want_message_bytes) {
      client->pending_message.append(std::string(buf->base + i, nread));
      client->want_message_bytes -= nread;
      break;
    } else {  // nread >= client->want_message_bytes.
      client->pending_message.append(
          std::string(buf->base + i, client->want_message_bytes));
      DLOG(client, "received message %s", client->pending_message.c_str());
      process_client_message((uv_handle_t *)stream);
      nread -= client->want_message_bytes;
      i += client->want_message_bytes;
      client->want_message_bytes = 0;
      client->got_length_bytes = 0;
    }
  }
}

static void send_message(uv_stream_t *stream, const std::string &message,
                         bool really_spammy = false) {
  Client *client = CLIENT(stream);
  uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
  const size_t len = message.size();
  char *dest = (char *)malloc(4 + len);
  dest[0] = (len >> 24) & 0xff;
  dest[1] = (len >> 16) & 0xff;
  dest[2] = (len >> 8) & 0xff;
  dest[3] = len & 0xff;
  memcpy(dest + 4, message.data(), len);
  uv_buf_t buf = uv_buf_init(dest, 4 + len);
  req->data = (void *)buf.base;
  if (verbose && !really_spammy) {
    CLOG(client, "sent message %s", message.c_str());
  }
  int r = uv_write(req, stream, &buf, 1, [](uv_write_t *req, int status) {
    Client *client = CLIENT(req->handle);
    if (status < 0) {
      CLOG(client, "reset because write failed: %s", uv_strerror(status));
      uv_close((uv_handle_t *)req->handle, cleanup_client_handle);
      // fallthrough to free buf
    }
    free(req->data);
    free(req);
  });
  if (r != 0) {
    CLOG(client, "reset because uv_write(!) failed: %s", uv_strerror(r));
    uv_close((uv_handle_t *)req->handle, cleanup_client_handle);
    free(req->data);
    free(req);
  }
}

typedef void (*client_visitor)(uv_handle_t *handle);

static void for_each_client(client_visitor visit) {
  uv_walk(uv_default_loop(),
          [](uv_handle_t *handle, void *arg) {
            if (handle->type != UV_TCP || handle->data == nullptr) {
              return;
            }
            ((client_visitor)arg)(handle);
          },
          (void *)visit);
}

static void start_polling_gamepad(uv_loop_t *loop);

static void keepalive(uv_timer_t *timer) {
  for_each_client([](uv_handle_t *handle) {
    send_message((uv_stream_t *)handle, "{\"message\": \"keep-alive\"}",
                 true /* really_spammy */);
  });
  if (!G.gamepad_present) {
    start_polling_gamepad(uv_default_loop());
  }
}

static void on_connection(uv_stream_t *server, int status) {
  if (status != 0) {
    GLOG("on_connection: %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t *handle = init_client_handle();
  int r = uv_tcp_init(uv_default_loop(), handle);
  if (r != 0) {
    GLOG("tcp_init: %s\n", uv_strerror(r));
    cleanup_client_handle((uv_handle_t *)handle);
    return;
  }

  r = uv_accept(server, (uv_stream_t *)handle);
  if (r != 0) {
    GLOG("accept: %s\n", uv_strerror(r));
    uv_close((uv_handle_t *)handle, cleanup_client_handle);
    return;
  }
  // no nagle, we're exchanging tiny messages on a private lan
  uv_tcp_nodelay(handle, 1);
  uv_os_fd_t fd;
  r = uv_fileno((uv_handle_t *)handle, &fd);
  if (r != 0) {
    GLOG("uv_fileno: %s\n", uv_strerror(r));
    uv_close((uv_handle_t *)handle, cleanup_client_handle);
    return;
  }
  // fail w ETIMEDOUT if missing acks for longer
  unsigned int timeout = ZOMBIE_SLAYER_MSEC;
  setsockopt(fd, SOL_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout));

  Client *client = CLIENT(handle);
  sockaddr_in addr;
  int name_len = sizeof(addr);
  if (uv_tcp_getpeername(handle, (sockaddr *)&addr, &name_len) == 0) {
    char host[32] = "?";
    uv_ip4_name(&addr, host, sizeof(host));
    int port = ntohs(addr.sin_port);
    client->id = G.connection_count++;
    client->name = std::string(host) + ":" + std::to_string(port);
  }
  CLOG(client, "connected!");
  r = uv_read_start((uv_stream_t *)handle,
                    [](uv_handle_t *handle, size_t ignored, uv_buf_t *buf) {
                      Client *client = CLIENT(handle);
                      buf->base = client->recv_buf;
                      buf->len = sizeof(client->recv_buf);
                    },
                    on_recv);
  if (r != 0) {
    CLOG(client, "reset because read_start failed: %s", uv_strerror(r));
    uv_close((uv_handle_t *)handle, cleanup_client_handle);
    return;
  }
}

static void set_display(uv_handle_t *handle, const std::string &text,
                        bool status = false) {
  picojson::object obj;
  obj["message"] = picojson::value(status ? "set-status" : "set-display");
  picojson::object data;
  data["message"] = picojson::value(text);
  obj["data"] = picojson::value(data);
  send_message((uv_stream_t *)handle, picojson::value(obj).serialize());
}

static void set_status(uv_handle_t *handle, const std::string &text) {
  set_display(handle, text, true /* status */);
}

static void set_progress(uv_handle_t *handle, int percent) {
  picojson::object obj;
  obj["message"] = picojson::value("set-progress");
  picojson::object data;
  data["value"] = picojson::value((double)clamp(percent, 0, 100));
  obj["data"] = picojson::value(data);
  send_message((uv_stream_t *)handle, picojson::value(obj).serialize(),
               true /* really_spammy */);
}

static int hull_integrity() {
  return clamp(5 + (G.good_commands / 3) - G.bad_commands, 0, 5);
}

static void set_integrity(uv_handle_t *handle) {
  picojson::object obj;
  obj["message"] = picojson::value("set-integrity");
  picojson::object data;
  double percent = (double)(100 * hull_integrity() / 5);
  data["value"] = picojson::value(clamp(percent, 0.0, 100.0));
  obj["data"] = picojson::value(data);
  send_message((uv_stream_t *)handle, picojson::value(obj).serialize(),
               true /* really_spammy */);
}

static MissionTiming mission_timing() {
  int mission = clamp(G.mission, 1, (int)ARRAYSIZE(MISSION_TIMING));
  return MISSION_TIMING[mission - 1];
}

static void clear_non_idle_displays(const char *status_text,
                                    const char *display_text) {
  for (auto handle : G.handles) {
    if (PANEL(handle).state == PANEL_READY ||
        PANEL(handle).state == PANEL_ACTIVE) {
      set_status(handle, status_text);
      set_display(handle, display_text);
      // set_progress(handle, 0);
    }
  }
}

static void send_integrity_to_non_idle() {
  for (auto handle : G.handles) {
    if (PANEL(handle).state == PANEL_READY ||
        PANEL(handle).state == PANEL_ACTIVE) {
      set_integrity(handle);
    }
  }
}

static void remove_all_non_idle_commands(const char *status_text,
                                         const char *display_text) {
  remove_command_if(
      [](const Command &command) { return !command.is_idle_command; });
  clear_non_idle_displays(status_text, display_text);
}

static void update_current_commands() {
  bool commands_changed = false;
  for (auto command = G.commands.begin(); command != G.commands.end();) {
    int progress = percent_left(now - command->started_tick,
                                command->deadline_tick - command->started_tick);
    set_progress(command->shower, progress);
    if (command->is_idle_command) {
      // idle commands are removed in panel_state_machine
      ++command;
      continue;
    }
    bool failed = now >= command->deadline_tick;
    bool succeeded = command->done;
    if (!failed && !succeeded) {
      ++command;
      continue;
    }
    if (failed) {
      DLOG(CLIENT(command->doer), "command failed: %s %s [shower %s]",
           command->id.c_str(), command->desired_state.c_str(),
           CLIENT(command->shower)->name.c_str());
      G.bad_commands++;
      set_status(command->shower, CHOOSE(BAD_MESSAGES));
      if (hull_integrity() == 1) {
        play_sound(ALARM_SOUND);
      } else {
        play_sound(BAD_COMMAND_SOUND);
      }
    } else {  // succeeded
      DLOG(CLIENT(command->doer), "command succeeded: %s %s [shower %s]",
           command->id.c_str(), command->desired_state.c_str(),
           CLIENT(command->shower)->name.c_str());
      int ticks_left = command->deadline_tick - now;
      G.score += COMMAND_SCORE_PER_SEC * GAME_TICK_MSEC * ticks_left / 1000;
      G.good_commands++;
      set_status(command->shower, CHOOSE(GOOD_MESSAGES));
      play_sound(GOOD_COMMAND_SOUND);
    }
    commands_changed = true;
    G.mission_command_count++;
    set_display(command->shower, "");
    // set_progress(command->shower, 100);
    PANEL(command->shower).shown_command_removed_tick = now;

    command = G.commands.erase(command);
  }
  if (commands_changed) {
    send_integrity_to_non_idle();
  }
}

static std::vector<Command>::iterator assign_command(uv_handle_t *doer,
                                                     uv_handle_t *shower,
                                                     int dt) {
  const Panel &panel = PANEL(doer);
  // pick a random control with eligible actions, then pick a random action for
  // that control
  int chosen_control = -1;
  for (int do_assignment = 0; do_assignment <= 1; do_assignment++) {
    int num_controls = 0;
    for (const auto &control : panel.controls) {
      bool control_already_has_command =
          std::any_of(G.commands.begin(), G.commands.end(),
                      [doer, control](const Command &command) {
            return command.doer == doer && command.id == control.id;
          });
      if (control_already_has_command) {
        continue;
      }
      int num_actions = 0;
      for (const auto &action : control.actions) {
        if (control.state != action.first /* state */) {
          num_actions++;
        }
      }
      if (num_actions > 0) {
        if (do_assignment && num_controls == chosen_control) {
          int actions_to_skip = rand() % num_actions;
          for (const auto &action : control.actions) {
            if (control.state != action.first /* state */) {
              if (actions_to_skip == 0) {
                Command command;
                command.id = control.id;
                command.action = action.second;
                command.desired_state = action.first;
                command.started_tick = now;
                command.deadline_tick = now + dt;
                command.doer = doer;
                command.shower = shower;
                return G.commands.insert(G.commands.end(), command);
              }
              actions_to_skip--;
            }
          }
          return G.commands.end();
        }
        num_controls++;
      }
    }
    if (num_controls == 0) {
      return G.commands.end();
    }
    chosen_control = rand() % num_controls;
  }
  return G.commands.end();
}

static void assign_new_commands() {
  std::vector<uv_handle_t *> non_idle;
  std::copy_if(G.handles.begin(), G.handles.end(), std::back_inserter(non_idle),
               [](uv_handle_t *handle) {
    return PANEL(handle).state == PANEL_READY ||
           PANEL(handle).state == PANEL_ACTIVE;
  });
  for (auto shower_handle : non_idle) {
    bool already_showing_command =
        std::any_of(G.commands.begin(), G.commands.end(),
                    [shower_handle](const Command &command) {
          return command.shower == shower_handle;
        });
    if (already_showing_command) {
      continue;
    }
    Panel *shower = &PANEL(shower_handle);
    MissionTiming timing = mission_timing();
    if (now - shower->shown_command_removed_tick >= timing.rest) {
      typedef std::pair<uv_handle_t *, int> handle_with_tiebreaker;
      std::vector<handle_with_tiebreaker> doers;
      for (auto doer : non_idle) {
        doers.push_back(std::make_pair(doer, rand()));
      }
      std::sort(doers.begin(), doers.end(),
                [](handle_with_tiebreaker a, handle_with_tiebreaker b) {
        int a_tick = PANEL(a.first).assigned_command_as_doer_tick;
        int b_tick = PANEL(b.first).assigned_command_as_doer_tick;
        return a_tick < b_tick || (a_tick == b_tick && a.second < b.second);
      });
      for (auto doer : doers) {
        auto doer_handle = doer.first;
        auto command =
            assign_command(doer_handle, shower_handle, timing.timeout);
        if (command != G.commands.end()) {
          DLOG(CLIENT(doer_handle), "command assigned: %s %s [shower %s]",
               command->id.c_str(), command->desired_state.c_str(),
               CLIENT(shower_handle)->name.c_str());
          set_status(shower_handle, "**Attention**");
          set_display(shower_handle, command->action);
          // set_progress(shower_handle, 100);
          shower->state = PANEL_ACTIVE;
          PANEL(doer_handle).state = PANEL_ACTIVE;
          PANEL(doer_handle).assigned_command_as_doer_tick = now;
          break;
        } else {
          DLOG(CLIENT(doer_handle), "failed to assign command");
        }
      }
    }
  }
}

static void panel_state_machine(uv_handle_t *handle) {
  Client *client = CLIENT(handle);
  Panel *panel = &client->panel;
  switch (panel->state) {
    case PANEL_NEW: {
      if (panel->controls.empty()) {
        break;
      }
      CLOG(client, "PANEL_NEW -> PANEL_IDLE");
      panel->state = PANEL_IDLE;
      // fallthrough to next case (don't wait a tick to prompt player)
    }
    case PANEL_IDLE: {
      auto command = std::find_if(G.commands.begin(), G.commands.end(),
                                  [handle](const Command &command) {
        return command.shower == handle;
      });
      if (command != G.commands.end() && command->done) {
        G.commands.erase(command);
        CLOG(client, "PANEL_IDLE -> PANEL_READY");
        panel->state = PANEL_READY;
        panel->ready_message_tick = now;
        set_status(handle, "**Ready!**");
        set_display(handle, "Wait for it...");
        // set_progress(handle, 100);
        set_integrity(handle);
        play_sound(JOIN_SOUND);
      } else if (command == G.commands.end() || now >= command->deadline_tick) {
        if (command != G.commands.end()) {
          G.commands.erase(command);
        }
        command =
            assign_command(handle, handle, PANEL_IDLE_COMMAND_TIMEOUT_TICKS);
        if (command != G.commands.end()) {
          command->is_idle_command = true;
          set_status(handle, "**Report for duty**");
          set_display(handle, command->action);
          // set_progress(handle, 100);
        }
      }
      break;
    }
    case PANEL_READY: {
      if (now - panel->last_state_change_tick >= PANEL_IDLE_AFTER_TICKS) {
        remove_associated_commands(handle);
        CLOG(client, "PANEL_READY -> PANEL_IDLE");
        panel->state = PANEL_IDLE;
        break;
      }
      if (now - panel->ready_message_tick >= PANEL_READY_MESSAGE_TICKS) {
        set_status(handle, "**Wait for it**");
        set_display(handle, CHOOSE(EXCUSES));
        panel->ready_message_tick = now;
      }
      G.num_ready_panels++;
      break;
    }
    case PANEL_ACTIVE: {
      if (now - panel->last_state_change_tick >= PANEL_IDLE_AFTER_TICKS) {
        remove_associated_commands(handle);
        CLOG(client, "PANEL_ACTIVE -> PANEL_IDLE");
        panel->state = PANEL_IDLE;
        break;
      }
      G.num_active_panels++;
      break;
    }
  }
}

static GamepadButtonMask map_gamepad_button(int sys_button) {
  switch (sys_button) {
    case 0:
      return X_BUTTON;
    case 1:
      return A_BUTTON;
    case 2:
      return B_BUTTON;
    case 3:
      return Y_BUTTON;
    case 4:
      return L_BUTTON;
    case 5:
      return R_BUTTON;
    case 8:
      return SELECT_BUTTON;
    case 9:
      return START_BUTTON;
  }
  return (GamepadButtonMask)0;
}

static void check_konami_code() {
  int next_code = KONAMI_CODE[G.konami_index];
  if (next_code) {
    if (G.gamepad_buttons) {
      if (G.gamepad_buttons == next_code) {
        G.konami_index++;
      } else {
        G.konami_index = 0;
      }
    }
  } else {
    if (!G.gamepad_buttons) {
      G.konami_index++;
    }
  }
  if (G.konami_index == ARRAYSIZE(KONAMI_CODE)) {
    G.konami_index = 0;
    G.konami_code = true;
  }
}

static void read_gamepad(uv_poll_t *poll, int status, int events) {
  uv_os_fd_t fd;
  int r = uv_fileno((uv_handle_t *)poll, &fd);
  if (r != 0) {
    goto error;
  }
  js_event e;
  while (read(fd, &e, sizeof(e)) > 0) {
    if (e.type & JS_EVENT_AXIS) {
      if (e.number == 0) {
        if (e.value < 0) {
          G.gamepad_buttons |= DPAD_LEFT;
        } else if (e.value > 0) {
          G.gamepad_buttons |= DPAD_RIGHT;
        } else {  // e.value == 0
          G.gamepad_buttons &= ~(DPAD_LEFT | DPAD_RIGHT);
        }
      } else if (e.number == 1) {
        if (e.value < 0) {
          G.gamepad_buttons |= DPAD_UP;
        } else if (e.value > 0) {
          G.gamepad_buttons |= DPAD_DOWN;
        } else {  // e.value == 0
          G.gamepad_buttons &= ~(DPAD_UP | DPAD_DOWN);
        }
      }
    } else if (e.type & JS_EVENT_BUTTON) {
      int button_mask = map_gamepad_button(e.number);
      if (e.value) {
        G.gamepad_buttons |= button_mask;
      } else {
        G.gamepad_buttons &= ~button_mask;
      }
    }
  }
  check_konami_code();
  G.gamepad_new_buttons |= G.gamepad_buttons;
  if (errno == EAGAIN) {
    return;
  }
  GLOG("read_gamepad: %s", strerror(errno));

error:
  G.gamepad_buttons = 0;
  G.gamepad_new_buttons = 0;
  G.gamepad_present = false;
  uv_poll_stop(poll);
  uv_close((uv_handle_t *)poll, nullptr);
}

static void send_display_update() {
  uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
  DisplayUpdate *update = new DisplayUpdate;
  update->now = now;
  update->mode = G.mode;
  update->score = G.score;
  update->play_count = G.play_count;
  update->hull_integrity = hull_integrity();
  int num_panels = 0;
  for (const auto *handle : G.handles) {
    Client *client = CLIENT(handle);
    update->panel_id[num_panels] = client->id;
    update->panel_state[num_panels] = client->panel.state;
    num_panels++;
    if (num_panels == PANEL_DISPLAY_SLOTS) {
      break;
    }
  }
  update->num_panels = num_panels;
  update->start_at_tick = G.start_at_tick;
  update->mission = G.mission;
  update->mission_start_tick = G.mission_start_tick;
  update->end_at_tick = G.end_at_tick;
  for (int i = 0; i < 3 && i < (int)G.initials.size(); i++) {
    update->initials[i] = G.initials[i];
  }
  update->initials[3] = 0;
  update->cur_initial = G.cur_initial;
  uv_buf_t buf = uv_buf_init((char *)update, sizeof(DisplayUpdate));
  req->data = (void *)buf.base;
  int r = uv_write(req, (uv_stream_t *)&display, &buf, 1,
                   [](uv_write_t *req, int status) {
    if (status < 0) {
      GLOG("display update failed: %s", uv_strerror(status));
    }
    delete (DisplayUpdate *)req->data;
    free(req);
  });
  if (r != 0) {
    GLOG("display update failed: %s", uv_strerror(r));
    delete (DisplayUpdate *)req->data;
    free(req);
  }
}

static void game(uv_timer_t *timer) {
  G.num_ready_panels = 0;
  G.num_active_panels = 0;
  for_each_client(panel_state_machine);
  switch (G.mode) {
    case ATTRACT: {
      if (G.num_ready_panels > 0) {
        GLOG("ATTRACT -> START_WAIT (%d ready)", G.num_ready_panels);
        G.mode = START_WAIT;
        G.start_at_tick = 0;
      }
      break;
    }
    case START_WAIT: {
      if (G.num_ready_panels == 0) {
        GLOG("START_WAIT -> ATTRACT");
        G.mode = ATTRACT;
      } else if (G.num_ready_panels == 1) {
        G.start_at_tick = 0;
      } else if (G.num_ready_panels >= 2) {
        if (G.start_at_tick == 0) {
          G.start_at_tick = now + START_WAIT_TICKS;
        } else if (now >= G.start_at_tick) {
          GLOG("START_WAIT -> SETUP_NEW_MISSION");
          G.mode = SETUP_NEW_MISSION;
        }
      }
      break;
    }
    case SETUP_NEW_MISSION: {
      // completion bonus for previous mission (if any)
      G.score += G.mission * MISSION_BONUS;
      G.mission++;
      G.mission_start_tick = now + MISSION_INTRO_TICKS;
      G.mission_end_tick = G.mission_start_tick + MISSION_TIME_LIMIT_TICKS;
      G.mission_command_count = 0;
      play_sound(START_MISSION_SOUND);
      GLOG("SETUP_NEW_MISSION -> NEW_MISSION");
      send_integrity_to_non_idle();
      G.mode = NEW_MISSION;
      break;
    }
    case NEW_MISSION: {
      if (now >= G.mission_start_tick) {
        GLOG("NEW_MISSION -> PLAYING");
        G.mode = PLAYING;
      }
      break;
    }
    case PLAYING: {
      if (G.num_ready_panels + G.num_active_panels < 2) {
        GLOG("PLAYING -> END_WAIT");
        G.mode = END_WAIT;
        G.end_at_tick = now + END_WAIT_TICKS;
        remove_all_non_idle_commands("**Need crew**", "Activate another panel");
      } else if (hull_integrity() == 0) {
        GLOG("PLAYING -> SETUP_GAME_OVER");
        G.mode = SETUP_GAME_OVER;
        remove_all_non_idle_commands("**Game over**",
                                     "Use controller to enter initials");
      } else if (G.mission_command_count >= mission_timing().command_limit ||
                 now >= G.mission_end_tick) {
        GLOG("PLAYING -> SETUP_NEW_MISSION");
        G.mode = SETUP_NEW_MISSION;
        remove_all_non_idle_commands("**New mission**", "Get ready!");
      } else {
        update_current_commands();
        assign_new_commands();
      }
      break;
    }
    case END_WAIT: {
      if (G.num_ready_panels + G.num_active_panels >= 2) {
        GLOG("END_WAIT -> PLAYING");
        G.mode = PLAYING;
        G.mission_command_count = 0;
        G.mission_end_tick = now + MISSION_TIME_LIMIT_TICKS;
        clear_non_idle_displays("**Game on**", "Get ready!");
      } else if (now >= G.end_at_tick) {
        GLOG("END_WAIT -> SETUP_GAME_OVER");
        G.mode = SETUP_GAME_OVER;
        clear_non_idle_displays("**Game over**",
                                "Use controller to enter initials");
      }
      break;
    }
    case SETUP_GAME_OVER: {
      G.mission = 0;
      G.mission_start_tick = 0;
      G.mission_end_tick = 0;
      G.mission_command_count = 0;
      G.start_at_tick = 0;
      G.end_at_tick = now + GAME_OVER_TICKS;
      G.bad_commands = 0;
      G.good_commands = 0;
      G.initials = "???";
      G.cur_initial = 0;
      play_sound(GAME_OVER_SOUND);
      GLOG("SETUP_GAME_OVER -> GAME_OVER");
      G.mode = GAME_OVER;
      // fallthrough
    }
    case GAME_OVER: {
      int buttons = G.gamepad_buttons | G.gamepad_new_buttons;
      if (buttons) {
        G.end_at_tick = now + GAME_OVER_TICKS;
      }
      if (buttons & DPAD_UP) {
        const char *letter = strchr(INITIALS, G.initials[G.cur_initial]);
        letter = (letter == nullptr || letter[1] == 0) ? INITIALS : letter + 1;
        G.initials[G.cur_initial] = *letter;
      } else if (buttons & DPAD_DOWN) {
        const char *letter = strchr(INITIALS, G.initials[G.cur_initial]);
        letter = (letter == nullptr || letter[0] == INITIALS[0])
                     ? INITIALS + strlen(INITIALS) - 1
                     : letter - 1;
        G.initials[G.cur_initial] = *letter;
      } else if (G.gamepad_new_buttons & (DPAD_LEFT | B_BUTTON)) {
        if (G.cur_initial > 0) G.cur_initial--;
      } else if (G.gamepad_new_buttons & (DPAD_RIGHT | A_BUTTON)) {
        if (G.cur_initial < 2) {
          G.cur_initial++;
        } else if (buttons & A_BUTTON) {
          G.end_at_tick = now;
        }
      } else if (G.gamepad_new_buttons & START_BUTTON) {
        G.end_at_tick = now;
      }
      if (now >= G.end_at_tick) {
        add_high_score(HighScore(G.play_count, G.initials, G.score));
        G.play_count++;
        G.score = 0;
        GLOG("GAME_OVER -> RESET_GAME");
        G.mode = RESET_GAME;
      }
      break;
    }
    case RESET_GAME: {
      for (auto handle : G.handles) {
        if (PANEL(handle).state == PANEL_READY ||
            PANEL(handle).state == PANEL_ACTIVE) {
          PANEL(handle).state = PANEL_IDLE;
        }
      }
      GLOG("RESET_GAME -> ATTRACT");
      G.mode = ATTRACT;
      break;
    }
  }
  G.gamepad_new_buttons = 0;
  if (G.konami_code) {
    play_sound(CONTRA_SOUND);
    G.konami_code = false;
  }
  send_display_update();
  now++;
}

static inline void must(int error_code, const char *prefix) {
  if (error_code != 0) {
    fprintf(stderr, "%s: %s\n", prefix, uv_strerror(error_code));
    exit(1);
  }
}

static void start_server(uv_tcp_t *server, uv_loop_t *loop) {
  // elsewhere, we assume that any tcp with non-null data is a client.
  ((uv_handle_t *)server)->data = nullptr;
  must(uv_tcp_init(loop, server), "tcp_init");

  sockaddr_in addr;
  must(uv_ip4_addr(server_ip, server_port, &addr), "address");
  must(uv_tcp_bind(server, (const struct sockaddr *)&addr, 0), "bind");
  must(uv_listen((uv_stream_t *)server, 128, on_connection), "listen");
}

static void start_polling_gamepad(uv_loop_t *loop) {
  int fd = open(gamepad_device, O_RDONLY | O_NONBLOCK);
  if (fd == -1) {
    GLOG("couldn't open gamepad: %s", strerror(errno));
    return;
  }
  int r = uv_poll_init(loop, &gamepad, fd);
  if (r != 0) {
    GLOG("poll_init: %s", uv_strerror(r));
    return;
  }
  r = uv_poll_start(&gamepad, UV_READABLE, read_gamepad);
  if (r != 0) {
    GLOG("poll_start: %s", uv_strerror(r));
    return;
  }
  G.gamepad_present = true;
  GLOG("gamepad connected");
}

static void start_keepalive_timer(uv_timer_t *timer, uv_loop_t *loop) {
  must(uv_timer_init(loop, timer), "timer_init");
  must(uv_timer_start(timer, keepalive, KEEPALIVE_MSEC, KEEPALIVE_MSEC),
       "timer_start");
}

static void start_game_timer(uv_timer_t *timer, uv_loop_t *loop) {
  must(uv_timer_init(loop, timer), "timer_init");
  must(uv_timer_start(timer, game, GAME_TICK_MSEC, GAME_TICK_MSEC),
       "timer_start");
}

static void start_display(uv_process_t *display_process, char *program,
                          uv_loop_t *loop) {
  must(uv_pipe_init(loop, &display, 0), "pipe_init");

  uv_process_options_t options;
  options.file = program;
  const char *args[2];
  args[0] = "display";
  args[1] = nullptr;
  options.args = (char **)args;
  options.env = nullptr;
  options.cwd = nullptr;
  options.flags = 0;
  uv_stdio_container_t child_stdio[3];
  child_stdio[0].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
  child_stdio[0].data.stream = (uv_stream_t *)&display;
  child_stdio[1].flags = UV_IGNORE;
  child_stdio[2].flags = UV_INHERIT_FD;
  child_stdio[2].data.fd = 2;
  options.stdio = child_stdio;
  options.stdio_count = 3;
  options.exit_cb =
      [](uv_process_t *process, int64_t exit_status, int term_signal) {
    GLOG("display process exit: status=%d signal=%d", exit_status, term_signal);
  };
  must(uv_spawn(loop, display_process, &options), "spawn");
}

extern int display_main();

int main(int argc, char **argv) {
  parse_command_line(argc, argv);
  if (strcmp(argv[0], "display") == 0) {
    display_main();
    return 0;
  }

  uv_tcp_t server;
  uv_timer_t keepalive_timer;
  uv_timer_t game_timer;
  uv_process_t display_process;
  uv_loop_t *loop = uv_default_loop();
  start_server(&server, loop);
  start_keepalive_timer(&keepalive_timer, loop);
  start_game_timer(&game_timer, loop);
  start_polling_gamepad(loop);
  start_display(&display_process, argv[0], loop);

  init_audio();
  atexit(cleanup_audio);
  play_music();

  int r = uv_run(loop, UV_RUN_DEFAULT);

  uv_loop_close(loop);
  return r;
}
