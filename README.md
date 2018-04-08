# Trekkin': "Boldly going forward, cause we can't find reverse."

Trekkin' is a multiplayer game like [Spaceteam](http://spaceteam.ca) played
with physical [control panels](https://github.com/igor47/spaceboard). This repo
has a Raspberry Pi program to coordinate gameplay and drive a cool central
display. It uses OpenGL ES2 for graphics and libuv for networking.

# Building and running

Build with `make` and run `trekkin`.

Intended to be run by systemd on a Raspberry Pi 3+.

TODO arguments.

# Details

## Gameplay

Each panel has a set of weird controls and a display. During play, a player's
panel periodically tells them some control on another panel needs to be
adjusted - so players end up simultaneously shouting and hunting for controls.
Commands must be enacted within a time limit. Gameplay continues at increasing
pace until too many commands are missed.

## Panel states

The server keeps a state machine per panel.

### Zombie ("it's worse than that, he's dead Jim")

This state exists to deal with soft failures during gameplay.

- Transitions in: any. 5 seconds waiting for a TCP ACK. Something is probably
  kaput, e.g. the panel crashed or someone pulled an Ethernet cable.
- Transitions out: `Idle` on network activity, else disconnect after 15
  seconds total. `Idle` rather than `Active` to avoid rejoining an ongoing
  game.

### Idle

- Transitions in: any. New connection, or no control changes for > 1 minute.
- Transitions out: `Ready` when player control verified.

Show a message asking to toggle a random control _on the panel_, to verify
someone is present and can read labels. The control changes every minute in
case the chosen one is broken.

### Ready

- Transitions in: `Idle`

A player has acknowledged they are at the panel.

### Active 

The panel is involved in the game and is a valid target for commands.

## Game states

The game has a state machine.

* Attract mode: No panels are ready. The game shows a title screen,
  flashy graphics, high scores, and generally attempts to interest nearby
  humans.
* Starting: One or more panels are ready. The game shows a message saying
  it's waiting for more players.
* Playing: Two or more panels are active. If new panels become ready they
  are made active.
* Ending: Only one panel is active. Wait for more to become ready for
  up to 10 seconds.
* Game over: 
* Diagnostic mode: Show connected panels and controls.


Panels connect to the
game server and advertise available inputs. Once the game begins,

A previous iteration of this game used the
[spacecontrol](https://github.com/wearhere/spacecontrol) software.


network server and 

It listens for state updates over a network connection and draws stuff.

# Implementation notes

I chose C-ish C++ cause.

## Network

The network 

## Display

The Raspberry Pi has a weird Broadcom GPU with its own compositing library and
an OpenGL ES2 driver, so that's what we use. You'll probably need a suitable Pi
to build and run it.

## Network

There's Klingons on the starboard bow, Jim.
It's life, Jim, but not as we know it.
Ye cannae change the laws of physics.
We come in peace -- shoot to kill, men.
It's worse than that, he's dead Jim.

# Links

* [Original control server](https://github.com/wearhere/spacecontrol).
