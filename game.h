#pragma once

#include <map>
#include <vector>
#include <string>

enum GameMode {
  // No panels are ready.
  ATTRACT = 0,
  // One or more panels are ready, waiting for more players.
  START_WAIT = 1,
  // Show new mission message.
  SETUP_NEW_MISSION = 2,
  NEW_MISSION = 3,
  // Two or more panels are active.
  PLAYING = 4,
  // Too many players have dropped out.
  END_WAIT = 5,
  // Splash screen cycle saying game over etc.
  SETUP_GAME_OVER = 6,
  GAME_OVER = 7,
  // Reset game state prior to returning to attract mode.
  RESET_GAME = 8,
};

const int GAME_TICK_MSEC = 100;
constexpr int ms_to_ticks(int msec) { return msec / GAME_TICK_MSEC; }

const int PANEL_IDLE_AFTER_TICKS = ms_to_ticks(120000);
const int PANEL_IDLE_COMMAND_TIMEOUT_TICKS = ms_to_ticks(15000);
const int PANEL_READY_MESSAGE_TICKS = ms_to_ticks(5000);

const int START_WAIT_TICKS = ms_to_ticks(10000);

const int MISSION_INTRO_TICKS = ms_to_ticks(5000);
const int MISSION_TIME_LIMIT_TICKS = ms_to_ticks(90000);
const int MISSION_COMMAND_LIMIT = 10;
const int MISSION_BONUS = 10000;

const int END_WAIT_TICKS = ms_to_ticks(15000);
const int GAME_OVER_TICKS = ms_to_ticks(10000);

const int COMMAND_SCORE_PER_SEC = 100;

enum PanelState {
  // Panel has not yet announced its controls.
  PANEL_NEW = 0,
  // Waiting for player.
  PANEL_IDLE = 1,
  // Player is present.
  PANEL_READY = 2,
  // Panel
  PANEL_ACTIVE = 3,
};

struct Command {
  Command()
      : started_tick(0),
        deadline_tick(0),
        done(false),
        is_idle_command(false),
        doer(nullptr),
        shower(nullptr) {}

  std::string id;
  std::string action;
  std::string desired_state;
  int started_tick;
  int deadline_tick;
  bool done;
  bool is_idle_command;
  uv_handle_t *doer;
  uv_handle_t *shower;
};

struct Control {
  std::string id;
  std::string state;
  std::map<std::string, std::string> actions;
};

struct Panel {
  Panel()
      : state(PANEL_NEW),
        last_state_change_tick(0),
        ready_message_tick(0),
        shown_command_removed_tick(0),
        assigned_command_as_doer_tick(0) {}

  PanelState state;
  int last_state_change_tick;
  int ready_message_tick;
  int shown_command_removed_tick;
  int assigned_command_as_doer_tick;
  std::vector<Control> controls;
};

struct Client {
  Client() : got_length_bytes(0), want_message_bytes(0) {}

  int id;
  std::string name;
  ssize_t got_length_bytes;
  ssize_t want_message_bytes;
  std::string pending_message;
  char recv_buf[16384];  // on unix just need one recv buffer per connection
  Panel panel;
};

enum GamepadButtonMask {
  X_BUTTON = 0x0001,
  A_BUTTON = 0x0002,
  B_BUTTON = 0x0004,
  Y_BUTTON = 0x0008,
  L_BUTTON = 0x0010,
  R_BUTTON = 0x0020,
  SELECT_BUTTON = 0x0040,
  START_BUTTON = 0x0080,
  DPAD_LEFT = 0x0100,
  DPAD_RIGHT = 0x0200,
  DPAD_UP = 0x0400,
  DPAD_DOWN = 0x0800,
};

struct Game {
  Game()
      : mode(ATTRACT),
        num_ready_panels(0),
        num_active_panels(0),
        start_at_tick(0),
        end_at_tick(0),
        mission(0),
        mission_start_tick(0),
        mission_end_tick(0),
        mission_command_count(0),
        good_commands(0),
        bad_commands(0),
        score(0),
        connection_count(0),
        play_count(0) {}

  GameMode mode;

  int num_ready_panels;
  int num_active_panels;

  int start_at_tick;
  int end_at_tick;

  int mission;
  int mission_start_tick;
  int mission_end_tick;
  int mission_command_count;

  int good_commands;
  int bad_commands;
  int score;
  int connection_count;
  int play_count;

  std::string initials;
  int cur_initial;

  int gamepad_buttons;
  int gamepad_new_buttons;
  bool gamepad_present;

  std::vector<Command> commands;
  std::vector<uv_handle_t *> handles;
};

// fixed size, no pointers
const int PANEL_DISPLAY_SLOTS = 8;
struct DisplayUpdate {
  int now;
  GameMode mode;
  int score;
  int play_count;
  int hull_integrity;
  int start_at_tick;
  int end_at_tick;
  int num_panels;
  int panel_id[PANEL_DISPLAY_SLOTS];
  PanelState panel_state[PANEL_DISPLAY_SLOTS];
  int mission;
  int mission_start_tick;
  char initials[4];
  int cur_initial;
};
