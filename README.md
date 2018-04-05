Display for spaceteam game

trekkin is a Raspberry Pi program to display graphics for a networked game. It
listens for state updates over a network connection and draws stuff.

The Raspberry Pi has a weird Broadcom GPU with its own compositing library and
an OpenGL ES2 driver, so that's what we use. You'll probably need a suitable Pi
to build and run it.
