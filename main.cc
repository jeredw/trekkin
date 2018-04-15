#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <sys/time.h>
#include <sys/socket.h>
#include <algorithm>

#include <uv.h>
#include "picojson/picojson.h"

#include "game.h"

#define CLIENT(x) ((Client *)(((uv_handle_t *)x)->data))
#define PANEL(x) (CLIENT(x)->panel)
#define ARRAYSIZE(xs) (sizeof(xs) / sizeof(xs[0]))
#define CHOOSE(xs) xs[rand() % ARRAYSIZE(xs)]
#define DLOG(...) \
  if (verbose) log(__VA_ARGS__)

const uint64_t KEEPALIVE_MSEC = 5000;           // send app level keepalives
const unsigned int ZOMBIE_SLAYER_MSEC = 10000;  // drop panels that don't ack

static const char *server_ip = "0.0.0.0";
static int server_port = 8000;
static bool verbose = false;

static Game G;       // global mc globalface
static int now = 0;  // current tick of game loop

static const char *const STARSHIP_NAMES[] = {
    "Argonaut",  "Bellerophon", "Conquistador", "Daedalus", "Endurance",
    "Fomalhaut", "Hyperion",    "Icarus",       "Jupiter",  "Nemesis",
    "Orion",     "Prometheus",  "Serenity",     "Titan",    "USS Budget",
    "Valkyrie",  "Yamato"};

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
    "bunkum!"
    "egad!",
    "hogwash!", "hokum!",
    "malarkey!"
    "poppycock!",
    "phooey!",
};

struct MissionTiming {
  int timeout;
  int rest;
  int command_limit;
};

static const MissionTiming MISSION_TIMING[] = {
    // timeout             rest
    {ms_to_ticks(20000), ms_to_ticks(10000), 10},
    {ms_to_ticks(20000), ms_to_ticks(5000), 15},
    {ms_to_ticks(15000), ms_to_ticks(5000), 20},
    {ms_to_ticks(10000), ms_to_ticks(0), 25},
    {ms_to_ticks(5000), ms_to_ticks(0), 30},
};

static void usage() {
  fprintf(stderr, "Usage: trekkin [-ip IP] [-port PORT] [-verbose]\n");
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
    } else {
      fprintf(stderr, "unrecognized option: %s\n", argv[0]);
      usage();
      exit(1);
    }
  }
}

static std::string timestamp() {
  char buf[32] = "";
  timeval tv;
  if (gettimeofday(&tv, nullptr) == 0) {
    tm *tmp = localtime(&tv.tv_sec);
    if (tmp != nullptr) {
      if (strftime(buf, sizeof(buf), "%T", tmp) != 0) {
        return std::string(buf) + "." + std::to_string(tv.tv_usec);
      }
    }
  }
  return std::string("");
}

static void log(Client *client, const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "[%s %s] ", timestamp().c_str(), client->name.c_str());
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

static void log(const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "[%s G] ", timestamp().c_str());
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
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
    log(client, "disconnected: %s", uv_strerror(nread));
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
          log(client, "reset due to %d byte message",
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
    log(client, "sent message %s", message.c_str());
  }
  int r = uv_write(req, stream, &buf, 1, [](uv_write_t *req, int status) {
    Client *client = CLIENT(req->handle);
    if (status < 0) {
      log(client, "reset because write failed: %s", uv_strerror(status));
      uv_close((uv_handle_t *)req->handle, cleanup_client_handle);
      // fallthrough to free buf
    }
    free(req->data);
    free(req);
  });
  if (r != 0) {
    log(client, "reset because uv_write(!) failed: %s", uv_strerror(r));
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

static void keepalive(uv_timer_t *timer) {
  for_each_client([](uv_handle_t *handle) {
    send_message((uv_stream_t *)handle, "{\"message\": \"keep-alive\"}",
                 true /* really_spammy */);
  });
}

static void on_connection(uv_stream_t *server, int status) {
  if (status != 0) {
    log("on_connection: %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t *handle = init_client_handle();
  int r = uv_tcp_init(uv_default_loop(), handle);
  if (r != 0) {
    log("tcp_init: %s\n", uv_strerror(r));
    cleanup_client_handle((uv_handle_t *)handle);
    return;
  }

  r = uv_accept(server, (uv_stream_t *)handle);
  if (r != 0) {
    log("accept: %s\n", uv_strerror(r));
    uv_close((uv_handle_t *)handle, cleanup_client_handle);
    return;
  }
  // no nagle, we're exchanging tiny messages on a private lan
  uv_tcp_nodelay(handle, 1);
  uv_os_fd_t fd;
  r = uv_fileno((uv_handle_t *)handle, &fd);
  if (r != 0) {
    log("uv_fileno: %s\n", uv_strerror(r));
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
    client->name =
        std::string(host) + ":" + std::to_string(ntohs(addr.sin_port));
  }
  log(client, "connected!");
  r = uv_read_start((uv_stream_t *)handle,
                    [](uv_handle_t *handle, size_t ignored, uv_buf_t *buf) {
                      Client *client = CLIENT(handle);
                      buf->base = client->recv_buf;
                      buf->len = sizeof(client->recv_buf);
                    },
                    on_recv);
  if (r != 0) {
    log(client, "reset because read_start failed: %s", uv_strerror(r));
    uv_close((uv_handle_t *)handle, cleanup_client_handle);
    return;
  }
}

static int clamp(int value, int min, int max) {
  if (value < min) {
    value = min;
  } else if (value > max) {
    value = max;
  }
  return value;
}

constexpr int percent_left(int elapsed, int total) {
  return 100 - (100 * elapsed / total);
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

static MissionTiming mission_timing() {
  int mission = clamp(G.mission, 1, 1 + ARRAYSIZE(MISSION_TIMING));
  return MISSION_TIMING[mission - 1];
}

static int hull_integrity() {
  return clamp(5 + (G.good_commands / 3) - G.bad_commands, 0, 5);
}

static void clear_non_idle_commands() {
  remove_command_if(
      [](const Command &command) { return !command.is_idle_command; });
}

static void update_current_commands() {
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
    } else {  // succeeded
      DLOG(CLIENT(command->doer), "command succeeded: %s %s [shower %s]",
           command->id.c_str(), command->desired_state.c_str(),
           CLIENT(command->shower)->name.c_str());
      int ticks_left = command->deadline_tick - now;
      G.score += COMMAND_SCORE_PER_SEC * GAME_TICK_MSEC * ticks_left / 1000;
      G.good_commands++;
      set_status(command->shower, CHOOSE(GOOD_MESSAGES));
    }
    G.mission_command_count++;
    set_display(command->shower, "");
    set_progress(command->shower, 100);
    PANEL(command->shower).shown_command_removed_tick = now;

    command = G.commands.erase(command);
  }
}

static std::vector<Command>::iterator assign_command(uv_handle_t *doer,
                                                     uv_handle_t *shower,
                                                     int dt) {
  const Panel &panel = PANEL(doer);
  int eligible_actions = 0;
  for (const auto &control : panel.controls) {
    bool existing_command_for_control =
        std::any_of(G.commands.begin(), G.commands.end(),
                    [doer, control](const Command &command) {
          return command.doer == doer && command.id == control.id;
        });
    if (!existing_command_for_control) {
      for (const auto &action : control.actions) {
        if (control.state != action.first /* state */) {
          eligible_actions++;
        }
      }
    }
  }
  if (eligible_actions == 0) {
    return G.commands.end();
  }
  int choice = rand() % eligible_actions;
  int i = 0;
  for (const auto &control : panel.controls) {
    for (const auto &action : control.actions) {
      if (i == choice) {
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
      i++;
    }
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
          set_status(shower_handle, "*** Attention ***");
          set_display(shower_handle, command->action);
          set_progress(shower_handle, 100);
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
      log(client, "PANEL_NEW -> PANEL_IDLE");
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
        log(client, "PANEL_IDLE -> PANEL_READY");
        panel->state = PANEL_READY;
        panel->ready_message_tick = now;
        set_status(handle, "*** Ready! ***");
        set_display(handle, "");
        set_progress(handle, 100);
      } else if (command == G.commands.end() || now >= command->deadline_tick) {
        command =
            assign_command(handle, handle, PANEL_IDLE_COMMAND_TIMEOUT_TICKS);
        if (command != G.commands.end()) {
          command->is_idle_command = true;
          set_status(handle, "*** Report for duty ***");
          set_display(handle, command->action);
          set_progress(handle, 100);
        }
      }
      break;
    }
    case PANEL_READY: {
      if (now - panel->last_state_change_tick >= PANEL_IDLE_AFTER_TICKS) {
        remove_associated_commands(handle);
        log(client, "PANEL_READY -> PANEL_IDLE");
        panel->state = PANEL_IDLE;
        break;
      }
      if (now - panel->ready_message_tick >= PANEL_READY_MESSAGE_TICKS) {
        set_status(handle, CHOOSE(EXCUSES));
        panel->ready_message_tick = now;
      }
      G.num_ready_panels++;
      break;
    }
    case PANEL_ACTIVE: {
      if (now - panel->last_state_change_tick >= PANEL_IDLE_AFTER_TICKS) {
        remove_associated_commands(handle);
        log(client, "PANEL_ACTIVE -> PANEL_IDLE");
        panel->state = PANEL_IDLE;
        break;
      }
      G.num_active_panels++;
      break;
    }
  }
}

static void game(uv_timer_t *timer) {
  G.num_ready_panels = 0;
  G.num_active_panels = 0;
  for_each_client(panel_state_machine);
  switch (G.mode) {
    case ATTRACT: {
      if (G.num_ready_panels > 0) {
        log("ATTRACT -> START_WAIT (%d ready)", G.num_ready_panels);
        G.mode = START_WAIT;
        G.start_at_tick = 0;
      }
      break;
    }
    case START_WAIT: {
      if (G.num_ready_panels == 0) {
        log("START_WAIT -> ATTRACT");
        G.mode = ATTRACT;
      } else if (G.num_ready_panels == 1) {
        G.start_at_tick = 0;
      } else if (G.num_ready_panels >= 2) {
        if (G.start_at_tick == 0) {
          G.start_at_tick = now + START_WAIT_TICKS;
        } else if (now >= G.start_at_tick) {
          log("START_WAIT -> SETUP_NEW_MISSION");
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
      log("SETUP_NEW_MISSION -> NEW_MISSION");
      G.mode = NEW_MISSION;
      break;
    }
    case NEW_MISSION: {
      if (now >= G.mission_start_tick) {
        log("NEW_MISSION -> PLAYING");
        G.mode = PLAYING;
      }
      break;
    }
    case PLAYING: {
      if (G.num_ready_panels + G.num_active_panels < 2) {
        log("PLAYING -> END_WAIT");
        G.mode = END_WAIT;
        G.end_at_tick = now + END_WAIT_TICKS;
        clear_non_idle_commands();
      } else if (hull_integrity() == 0) {
        log("PLAYING -> SETUP_GAME_OVER");
        G.mode = SETUP_GAME_OVER;
        clear_non_idle_commands();
      } else if (G.mission_command_count >= mission_timing().command_limit ||
                 now >= G.mission_end_tick) {
        log("PLAYING -> SETUP_NEW_MISSION");
        G.mode = SETUP_NEW_MISSION;
        clear_non_idle_commands();
      } else {
        update_current_commands();
        assign_new_commands();
      }
      break;
    }
    case END_WAIT: {
      if (G.num_ready_panels + G.num_active_panels >= 2) {
        log("END_WAIT -> PLAYING");
        G.mode = PLAYING;
        G.mission_command_count = 0;
        G.mission_end_tick = now + END_WAIT_TICKS;
      } else if (now >= G.end_at_tick) {
        log("END_WAIT -> SETUP_GAME_OVER");
        G.mode = SETUP_GAME_OVER;
      }
      break;
    }
    case SETUP_GAME_OVER: {
      G.play_count++;
      G.mission = 0;
      G.mission_start_tick = 0;
      G.mission_end_tick = 0;
      G.mission_command_count = 0;
      G.start_at_tick = 0;
      G.end_at_tick = now + GAME_OVER_TICKS;
      G.bad_commands = 0;
      G.good_commands = 0;
      for (auto handle : G.handles) {
        if (PANEL(handle).state == PANEL_READY ||
            PANEL(handle).state == PANEL_ACTIVE) {
          set_status(handle, "*** Game over ***");
          set_display(handle, "Thank you for playing!");
          set_progress(handle, 0);
        }
      }
      log("SETUP_GAME_OVER -> GAME_OVER");
      G.mode = GAME_OVER;
      // fallthrough
    }
    case GAME_OVER: {
      if (now >= G.end_at_tick) {
        log("GAME_OVER -> RESET_GAME");
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
      log("RESET_GAME -> ATTRACT");
      G.mode = ATTRACT;
      break;
    }
  }
  now++;
}

static inline void must(int error_code, const char *prefix) {
  if (error_code != 0) {
    fprintf(stderr, "%s: %s", prefix, uv_strerror(error_code));
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

int main(int argc, char **argv) {
  parse_command_line(argc, argv);

  uv_tcp_t server;
  uv_timer_t keepalive_timer;
  uv_timer_t game_timer;
  uv_loop_t *loop = uv_default_loop();
  start_server(&server, loop);
  start_keepalive_timer(&keepalive_timer, loop);
  start_game_timer(&game_timer, loop);

  int r = uv_run(loop, UV_RUN_DEFAULT);

  uv_loop_close(loop);
  return r;
}
