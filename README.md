# Trekkin': "Boldly going forward, cause we can't find reverse."

Trekkin' is a multiplayer game like [Spaceteam](http://spaceteam.ca) played
with physical [control panels](https://github.com/igor47/spaceboard). This
repository has a Raspberry Pi program to coordinate gameplay and drive a cool
central display. It uses OpenGL ES 2.0 for graphics and libuv for networking.

# Usage

Build with `make` and run `trekkin` with desired options:

* `-ip IPADDR` ip address server should listen on
* `-port PORT` tcp port server should listen on
* `-verbose` spew debug messages to stderr
* `-gamepad DEVICE` path to gamepad device file
* `-sfx` play sound effects and music

At present, ye cannae change the laws of physics.

# Dependencies

- `libuv1`: raspbian jessie only has `libuv-0.10` so the libuv submodule has
  the right version.
- `picojson`: in the picojson submodule.
- `/opt/vc/lib`: these ship with raspbian jessie.
- `stb`: <3 [stb](https://github.com/nothings/stb). Using `stb_truetype.h`,
  `stb_image.h` in `stb`.
- `SDL2`: `SDL_mixer` is used for audio.
- Written for Raspberry Pi 3 which has a decent embedded GPU. Be sure the GPU
  has at least 128MB RAM and you are not using the experimental DRM driver. My
  `/boot/config.txt` has:

```
#dtoverlay=vc4-kms-v3d
gpu_mem=128
```

# Notes

## Gameplay overview

Each panel has a set of weird controls and a display. During play, a player's
panel display periodically tells them some control (which may be on a different
panel) needs to be adjusted - so players end up simultaneously shouting and
hunting for controls. Commands must be done within a time limit. Gameplay
continues at increasing pace until too many commands are missed.

## Panels

Panels have their own computers that connect to the trekkin server over TCP on
a LAN. The server keeps a state machine per panel. Panels may be `new`, `idle`,
`ready`, or `active`.

### `new`

Panels are `new` when they first connect to the server. After they announce
available controls they become `idle`.

### `idle`

In the `idle` state, we ask the player to toggle a random control _on their
panel_ to verify they are present and can read labels and push buttons. The
random control changes every 15 seconds in case the chosen one is broken.
The status message says to "report for duty".

A panel transitions to `idle` from the `ready` or `active` states if none of
its controls change for 90 seconds, the length of a mission, as this probably
means someone walked away from the game. Panels also become `idle` on game
over so that a new game doesn't begin right away.

### `ready`

In the `ready` state a player is at the panel, but the panel is not part of the
game yet and doesn't have commands. After every 5 seconds waiting in `ready`,
panels show a random ridiculous loading message.

A panel transitions to `ready` from `idle` when the idle command is done.
There should be a satisfying sound like a big capacitor charging up.

### `active`

Panels in the `active` state are part of an ongoing game and may have commands.
Panels transition to `active` when the game first assigns them a command.

## Game states

There is also a state machine for the overall game.

* Attract mode: No panels are ready. The game shows a flashy title screen,
  high scores, and generally attempts to interest nearby humans.
* Start wait: After one or more panels are ready, the game picks the next in an
  alphabetical list of starships and shows a message saying that ship is
  waiting for more crew. The game starts 10 seconds after two panels are ready.
* New mission: Show a screen for 5 seconds before each mission begins.
* Playing: Two or more panels are ready or active. New players can join any
  time. See below for how play works.
* End wait: A game is going on but fewer than two panels are ready or active.
  Wait for at least 2 panels to become ready for up to 15 seconds. This is in
  case a panel flakes out or people want to tag in on an existing game.
* Game over: The game ended either because too many commands failed or
  players left. All panels are marked idle, the screen says "game over" for 10
  seconds, and we go back to attract mode.

### Detailed gameplay

The game is broken up into _missions_ of 90 seconds or some number of commands,
whichever comes first. Each mission has a timeout for commands and a rest time
that must pass before a panel can show a new command.

| mission# | timeout | rest time  | commands |
| -------- | ------- | ---------- | -------- |
| 1        | 20 sec  |  5 sec     | 10       |
| 2        | 20 sec  |  5 sec     | 15       |
| 3        | 15 sec  |  5 sec     | 20       |
| 4        | 10 sec  |  0 sec     | 25       |
| 5+       |  5 sec  |  0 sec     | 30       |

The starship initially has "hull integrity" of 5. Hull integrity decreases by
one point for each command missed and increases by one (up to its max) for
every three commands completed. The game is over when hull integrity is 0.
(When hull integrity hits 2, a hull integrity warning starts flashing, and at
hull integrity 1 a klaxon sounds.)

The team's score is a sum of seconds remaining for each completed command times
100, to incentivize playing faster initially, plus a bonus of 10,000 times
mission# per completed mission.

Each panel shows one command at a time, which may be either for that panel or
for another panel. Players should be doing a mix of pushing buttons and telling
other people to push buttons. When assigning commands to show on each panel, we
always choose the panel that was least recently assigned a command to _do_,
breaking ties randomly.

## Networking

The game uses TCP on a LAN with small messages so clients and servers should
turn off Nagle's algorithm. The server resets a client connection when
- It's been more than 10 seconds waiting for a TCP ACK.
- There is a likely protocol framing error (a message > 150k).
- There is a non `EAGAIN` read/write error.
- Something is wrong with libuv (hopefully not).
- It's worse than that - he's dead, Jim.

### Protocol

Messages are UTF-8 encoded JSON preceded by a 4 byte big-endian length.

### Client->Server: `announce`

```json
{
   "message" : "announce",
   "data" : {
      "controls" : [
         {
            "state" : "current state",
            "actions" : {
               "0" : "Turn it off",
               "1" : "Turn it on",
               "2" : ""
            },
            "id" : "internal id"
         }
      ]
   }
}
```

The `announce` message advertises controls, their initial states, and available
actions. action values are shown to players verbatim during gameplay. Empty
action values are never shown to players, so may be useful for the inactive
state of pushbuttons.

Clients may send `announce` messages at any time in case controls break or
change or whatever. The server drops any pending commands for clients that
announce after play has begun, and uses the newly announced set of controls for
future commands.

### Client->Server: `set-state`

```json
{
   "message" : "set-state",
   "data" : {
      "id" : "id of control",
      "state" : "state label"
   }
}
```

Sets the state of control `id` to `state`. This is how the server knows that
some command was done.

### Server->Client: `set-display`

```json
{
   "message" : "set-display",
   "data" : {
      "message" : "Text to display."
   }
}
```

Prints some permanent text on a panel's display. Used to communicate the
current command that each panel shows.

### Server->Client: `set-status`

```json
{
   "message" : "set-status",
   "data" : {
      "message" : "A transient status message."
   }
}
```

Flashes a transient status message on a panel's display. Used for commentary
about what's happening in the game to keep things interesting.

### Server->Client: `set-progress`

```json
{
   "message" : "set-progess",
   "data" : {
      "value" : 42
   }
}
```

Sets a progress bar located somewhere on a panel to an integer percentage
between 0 and 100. This is used to count down the time remaining for commands.

### Server->Client: `set-integrity`

```json
{
   "message" : "set-integrity",
   "data" : {
      "value" : 20
   }
}
```

Informs panels of the current hull integrity as an integer percentage between 0
and 100.

## High scores

The game stores the top ten high scores in a text file `high_scores.txt` in
decreasing order by score. The file has one score record per line. Records are
in plain text with fields separated by spaces. The fields are: play number
(since startup), player initials, score.

## Display

The Raspberry Pi has a weird Broadcom GPU with its own compositing library
("dispman") and an OpenGL ES 2.0 driver, so that's what we use.

The display program runs in a child process and reads updates from a pipe on
its stdin. The updates are a small fixed size binary message that says what's
going on, e.g. what mode the game is in and what the current score is. Based on
these messages, every frame, it lays out what to draw and then submits that to
the GPU. Most of the time the program should be idle waiting for dispman to
signal a vertical sync (e.g. at 60 Hz).

Note: While we're using SDL and its video library supports dispman, the
raspbian package seems to only allow the X11 compositor. It's possible to get
SDL to use dispman by building it manually, but to simplify things we just call
dispman ourselves.

## Sound effects and music

If enabled, music and sound effects are played from the main process because
that's the simplest place to do it. `SDL_mixer` has its own thread for mixing
audio, which shouldn't block the event loop, and seems to use minimal CPU.

## Gamepad

Players can enter their initials on the game over screen using an attached
gamepad. The main program uses libuv to poll the gamepad using the Linux
joystick API. If a gamepad is not present or is disconnected, the program
periodically tries to reconnect - so gamepads can be hotswapped in case one
breaks.

# Links

* Old [control server](https://github.com/wearhere/spacecontrol)
* [libuv docs](http://docs.libuv.org/en/v1.x/index.html)
* [OpenGL ES 2.0 quick reference](https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf) 
* [Linux Joystick API](https://www.kernel.org/doc/Documentation/input/joystick-api.txt)
* [SDL mixer docs](https://www.libsdl.org/projects/SDL_mixer/docs/SDL_mixer.html)
