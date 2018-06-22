#!/usr/bin/env python
import curses
import time
from curses import wrapper
import random
import signal
import socket
import sys

from client import Client

fragments = lambda: [".x"[random.randint(0, 1)] for _ in range(26)]
baud_rates = ["110", "300", "600", "1200", "2400", "4800", "9600", "14400", "19200", "38400", "57600"]
controls = {
    "power": {
        "state": "less",
        "actions": {
            "less": "less power!",
            "more": "more power!",
        },
        "toggle": ["less", "more"],
    },
    "turbo": {
        "state": "16",
        "actions": {
            "16": "set turbo to 16MHz",
            "33": "set turbo to 33MHz",
        },
        "toggle": ["16", "33"],
    },
    "field": {
        "state": "e",
        "actions": {
            "n": "Set E field to north",
            "ne": "Set E field to northeast",
            "e": "Set E field to east",
            "se": "Set E field to southeast",
            "s": "Set E field to south",
            "sw": "Set E field to southwest",
            "w": "Set E field to west",
            "nw": "Set E field to northwest",
        },
        "toggle": ["n", "ne", "e", "se", "s", "sw", "w", "nw"],
    },
    "logic": {
        "state": "clear",
        "actions": {
            "clear": "set logic to clear",
            "fuzzy": "engage fuzzy logic",
        },
        "toggle": ["clear", "fuzzy"],
    },
    "river": {
        "state": "ford",
        "actions": {
            "ford": "Ford the river",
            "float": "Float the wagon",
            "pay": "Pay the Indian",
        },
        "toggle": ["ford", "float", "pay"],
    },
    "adrive": {
        "state": "abort",
        "actions": {
            "abort": "A: drive abort",
            "retry": "A: drive retry",
            "fail": "A: drive fail",
        },
        "toggle": ["abort", "retry", "fail"],
    },
    "xreg": {
        "state": "0",
        "actions": dict((str(x), "set x register to %d" % (x,))
                        for x in range(20)),
        "toggle": [str(x) for x in range(20)],
    },
    "gibson": {
        "state": "0",
        "actions": {
            "0": "",
            "hacked": "Hack the Gibson",
        },
        "off": "0",
        "on": "hacked",
        "timer": 0,
    },
    "security": {
        "state": "0",
        "actions": {
            "0": "",
            "uhuhuh": "",
            "firewall": "",
            "accessed": "Access main security grid",
        },
        "on": "accessed",
        "off": "0",
        "timer": 0,
    },
    "writeprotect": {
        "state": "writeable",
        "actions": {
            "writeable": "Disable write protection",
            "protected": "Enable write protection",
        },
        "toggle": ["writeable", "protected"],
    },
    "baud": {
        "state": "300",
        "actions": dict((b, "Set baud rate {}".format(b))
                        for b in baud_rates),
        "toggle": baud_rates,
    },
    "editor": {
        "state": "vi",
        "actions": {
            "vi": "Switch editor to vi",
            "emacs": "Switch editor to emacs",
            "ed": "Switch editor to ed",
        },
        "toggle": ["vi", "emacs", "ed"],
    },
    "cipher": {
        "state": "des",
        "actions": {
            "des": "Set cipher to des",
            "rc5": "Set cipher to rc5",
            "rot13": "Set cipher to rot13",
        },
        "toggle": ["des", "rc5", "rot13"],
    },
    "defrag": {
        "state": "0",
        "actions": {
            "0": "",
            "defragged": "Defrag hard drive",
        },
        "drive": fragments(),
        "off": "0",
        "timer": 0,
    },
    "irq": {
        "state": "0",
        "actions": dict((str(n), "Select IRQ {}".format(n))
                        for n in range(8)),
        "toggle": [str(n) for n in range(8)]
    },
    "snake": {
        "state": "0",
        "actions": {
            "0": "",
            "crashed": "Crash Snake",
        },
        "off": "0",
        "on": "crashed",
        "timer": 0,
    },
    "java": {
        "state": "0",
        "actions": {
            "0": "",
            "start1": "",
            "start2": "",
            "start3": "",
            "run": "Run Java",
        },
        "off": "0",
        "on": "run",
        "timer": 0,
    },
    "modem": {
        "state": "0",
        "actions": {
            "0": "",
            "dial5551212": "Connect modem to 555-1212",
            "dial8675309": "Connect modem to 867-5309",
            "dial6492568": "Connect modem to 649-2568",
            "dial7779311": "Connect modem to 777-9311",
        },
        "off": "0",
        "number": "",
        "timer": 0,
    }
}
progress = 0
integrity = 100
status = ""
display = ""
snake_dx, snake_dy = 0,1
snake_timer = 0
snake = [(30, 13)]
apple_timer = 0
apples = []

def activate(name, key):
    global controls
    cur_state = controls[name]["state"]
    if "toggle" in controls[name]:
        toggle = controls[name]["toggle"]
        controls[name]["state"] = toggle[(1 + toggle.index(cur_state)) % len(toggle)]
    elif "on" in controls[name]:
        if name == "security" and random.random() < 0.5:
            controls[name]["state"] = "uhuhuh"
        elif name == "gibson" and controls["security"]["state"] != "accessed":
            controls["security"]["state"] = "firewall"
            controls["security"]["timer"] = 30
            return
        elif name == "java":
            controls[name]["state"] = "start1"
        else:
            controls[name]["state"] = controls[name]["on"]
        controls[name]["timer"] = 30
    elif name == "modem":
        if controls[name]["state"] == "0":
            if len(controls[name]["number"]) < 7:
                controls[name]["number"] += key
            if len(controls[name]["number"]) == 7:
                for i in range(5):
                    curses.beep()
                controls[name]["state"] = "dial" + controls[name]["number"]
                controls[name]["timer"] = 30
    elif name == "defrag" and controls["defrag"]["state"] == "0":
        drive = controls["defrag"]["drive"]
        try: free = drive.index('.')
        except ValueError: free = -1
        if free != -1:
            try: nonfree = drive.index('x', free)
            except ValueError: nonfree = -1
            if nonfree != -1:
                drive[free], drive[nonfree] = 'x', '.'
                return
        controls[name]["state"] = "defragged"
        controls[name]["timer"] = 30

def draw_power(s):
    s.addstr(4, 2, "(P)ower")
    s.addstr(5, 3, " less")
    s.addstr(6, 3, " more")
    s.addch(5 if controls["power"]["state"] == 'less' else 6, 3, ">")

def draw_turbo(s):
    s.addstr(4, 11, "(T)urbo")
    s.addstr(5, 12, " 16 MHz")
    s.addstr(6, 12, " 33 MHz")
    s.addch(5 if controls["turbo"]["state"] == '16' else 6, 12, ">")

def draw_field(s):
    s.addstr(4, 20, "(F)ield")
    s.addstr(5, 21, "   N     ")
    s.addstr(6, 21, " W   E   ")
    s.addstr(7, 21, "   S     ")
    state = controls["field"]["state"]
    s.addch(6, 24, "-" if state in ("e", "w") else
            "v" if state in ("s") else
            "^" if state in ("n") else
            "/" if state in ("ne", "sw") else "\\")
    if state == "e":
        s.addch(6, 25, ">")
    elif state == "w":
        s.addch(6, 23, "<")

def draw_logic(s):
    s.addstr(4, 29, "(L)ogic")
    s.addstr(5, 30, " clear")
    s.addstr(6, 30, " fuzzy")
    s.addch(5 if controls["logic"]["state"] == 'clear' else 6, 30, ">")

def draw_river(s):
    s.addstr(19, 2, "(R)iver")
    s.addstr(20, 3, " ford")
    s.addstr(21, 3, " float wagon")
    s.addstr(22, 3, " pay indian")
    state = controls["river"]["state"]
    s.addch(20 if state == 'ford' else
            21 if state == 'float' else
            22, 3, ">")

def draw_adrive(s):
    s.addstr(19, 16, "(A):drive")
    s.addstr(20, 17, " abort")
    s.addstr(21, 17, " retry")
    s.addstr(22, 17, " fail")
    state = controls["adrive"]["state"]
    s.addch(20 if state == 'abort' else
            21 if state == 'retry' else
            22, 17, ">")

def draw_gibson(s):
    if controls["gibson"]["state"] == "hacked":
        s.addstr(17, 2, "(G)1b50/\/")
    else:
        s.addstr(17, 2, "(G)ibson  ")

def draw_security(s):
    s.addstr(13, 2, "(S)ecurity grid ")
    s.addstr(14, 3, "   /_/_/_/_/    ")
    s.addstr(15, 3, "  /_/_/_/_/     ")
    s.addstr(16, 3, " /_/_/_/_/      ")
    if controls["security"]["state"] == "uhuhuh":
        s.addstr(14, 3, "(UH UH UH! YOU )")
        s.addstr(15, 3, "(DIDN'T SAY THE)")
        s.addstr(16, 3, "(MAGIC WORD... )")
    elif controls["security"]["state"] == "accessed":
        s.addstr(14, 3, "   /_/_/_/_/    ")
        s.addstr(15, 3, "  ACCESSED      ")
        s.addstr(16, 3, " /_/_/_/_/      ")
    elif controls["security"]["state"] == "firewall":
        s.addstr(14, 3, "   /_/_/_/_/    ")
        s.addstr(15, 3, " (firewall)     ")
        s.addstr(16, 3, " /_/_/_/_/      ")

def draw_xreg(s):
    s.addstr(8, 64, "(X)-Register")
    s.addstr(9, 65,  " .---.---. ")
    s.addstr(10, 65, " | o | o | ")
    s.addstr(11, 65, " | o | o | ")
    s.addstr(12, 65, " |   | o | ")
    s.addstr(13, 65, " +---+---+ ")
    s.addstr(14, 65, " | o | o | ")
    s.addstr(15, 65, " | o | o | ")
    s.addstr(16, 65, " | o | o | ")
    s.addstr(17, 65, " | o | o | ")
    s.addstr(18, 65, " | o | o | ")
    s.addstr(19, 65, " | o | o | ")
    s.addstr(20, 65, " '---'---' ")
    state = int(controls["xreg"]["state"])
    s.addch(11 if (state % 10) >= 5 else 12, 72, " ")
    s.addch(14 + (state % 5), 72, " ")
    s.addch(14 + ((state / 10) % 5), 68, " ")

def draw_writeprotect(s):
    s.addstr(4, 64, "(W)")
    s.addstr(4, 69, ".-.")
    if controls["writeprotect"]["state"] == "writeable":
        s.addstr(5, 69, "|*|")
        s.addstr(6, 69, "|o|")
    else:
        s.addstr(5, 69, "| |")
        s.addstr(6, 69, "|*|")

def draw_baud(s):
    s.addstr(21, 64, "(B)aud {:6s}".format(controls["baud"]["state"]))

def draw_irq(s):
    s.addstr(22, 64, "(I)RQ {}".format(controls["irq"]["state"]))

def draw_editor(s):
    s.addstr(8, 2, "(E)ditor")
    s.addstr(9, 3, " vi")
    s.addstr(10, 3, " emacs")
    s.addstr(11, 3, " ed")
    state = controls["editor"]["state"]
    s.addch(9 if state == 'vi' else
            10 if state == 'emacs' else 11, 3, ">")

def draw_cipher(s):
    s.addstr(8, 11, "(C)ipher")
    s.addstr(9, 12, " des")
    s.addstr(10, 12, " rc5")
    s.addstr(11, 12, " rot13")
    state = controls["cipher"]["state"]
    s.addch(9 if state == 'des' else
            10 if state == 'rc5' else 11, 12, ">")

def draw_defrag(s):
    s.addstr(19, 27, "(D)efrag")
    if controls["defrag"]["state"] == "defragged":
        curses.beep()
        s.addstr(22, 27, ' 26 SECTORS OK')
    else:
        s.addstr(22, 27, '              ')
    s.addstr(20, 27, "|{}|".format(''.join(controls["defrag"]["drive"][:13])))
    s.addstr(21, 27, "|{}|".format(''.join(controls["defrag"]["drive"][13:])))

def draw_snake(s):
    s.addstr(8, 20, '.' * 20)
    s.addstr(9, 20, '.' * 20)
    s.addstr(10, 20, '.' * 20)
    s.addstr(11, 20, '.' * 20)
    s.addstr(12, 20, '.' * 20)
    s.addstr(13, 20, '.' * 20)
    s.addstr(14, 20, '.' * 20)
    s.addstr(15, 20, '.' * 20)
    s.addstr(16, 20, '.' * 20)
    s.addstr(17, 20, '.' * 20)
    for x, y in snake:
        s.addch(y, x, "*")
    for x, y in apples:
        s.addch(y, x, "&")

def next_snake_pos(x, y, dx, dy):
    x += dx
    y += dy
    if x < 20: x = 39
    elif x > 39: x = 20
    if y < 8: y = 17
    elif y > 17: y = 8
    return x, y

def drive_snake(dx, dy):
    global snake_dx
    global snake_dy
    if len(snake) >= 2:
        x, y = next_snake_pos(snake[-1][0], snake[-1][1], dx, dy)
        if (x, y) == snake[-2]:
            return
    snake_dx, snake_dy = dx, dy

def update_snake(s):
    global apple_timer
    global apples
    global snake_timer
    global snake
    global controls
    snake_timer += 1
    if snake_timer == 4:
        snake_timer = 0
        x, y = next_snake_pos(snake[-1][0], snake[-1][1], snake_dx, snake_dy)
        if (x, y) in snake:
            apples = []
            snake = [(30, 13)]
            draw_snake(s)
            controls["snake"]["state"] = "crashed"
            controls["snake"]["timer"] = 10
        elif (x, y) in apples:
            apples.remove((x, y))
            snake.append((x, y))
            s.addch(y, x, '*')
        else:
            tx, ty = snake.pop(0)
            s.addch(ty, tx, '.')
            snake.append((x, y))
            s.addch(y, x, '*')
    apple_timer += 1
    if apple_timer == 50:
        apple_timer = 0
        x, y = random.randint(20, 39), random.randint(8, 17)
        if not (x, y) in apples and not (x, y) in snake:
            apples.append((x, y))
            s.addch(y, x, '&')

def draw_java(s):
    s.addstr(8, 41, "(J)ava")
    state = controls["java"]["state"]
    if state == "0":
        s.addstr(9, 42, "            ")
    elif state == "start1":
        s.addstr(9, 42, "loading jars")
    elif state == "start2":
        s.addstr(9, 42, "optimizing  ")
    elif state == "start3":
        s.addstr(9, 42, "gc pause    ")
    elif state == "run":
        s.addstr(9, 42, ".....success")

def draw_modem(s):
    s.addstr(11, 42, "Modem            ")
    if controls["modem"]["state"] == "0": 
        s.addstr(12, 42, " .____________.  @,")
        s.addstr(13, 42, "/    ______    \@  ")
        s.addstr(14, 42, "\____/    \____/   ")
        s.addstr(15, 42, "\----/____\----/__.")
        s.addstr(16, 42, "'--------------'   ")
        s.addstr(17, 42, "   (1) (2) (3)     ")
        s.addstr(18, 42, "   (4) (5) (6)     ")
        s.addstr(19, 42, "   (7) (8) (9)     ")
        s.addstr(20, 42, "   L*I (0) L#I     ")
    else:
        s.addstr(12, 42, "                   ")
        s.addstr(13, 42, " .____________.  @,")
        s.addstr(14, 42, "/    ______    \@  ")
        s.addstr(15, 42, "\----/____\----/__.")
        s.addstr(16, 42, "'--------------'   ")
        s.addstr(17, 42, "   (1) (2) (3)     ")
        s.addstr(18, 42, "   (4) (5) (6)     ")
        s.addstr(19, 42, "   (7) (8) (9)     ")
        s.addstr(20, 42, "   L*I (0) L#I     ")
    s.addstr(21, 42, "  [ATDT{:7s}]    ".format(controls["modem"]["number"]))

def draw_integrity(s):
    s.addstr(23, 68, "----------")
    s.addstr(23, 68, "Hull: {}%".format(integrity))

def draw_progress(s):
    s.addstr(4, 42, '  ' * 10)
    s.addstr(5, 42, '@ ' * 10)
    num_lit = int(progress / 10)
    num_lit = 0 if num_lit < 0 else 10 if num_lit > 10 else num_lit
    for i in range(num_lit):
        s.addch(4, 42 + 2 * i, ',')
        s.addch(5, 42 + 2 * i, '@', curses.A_REVERSE)
    s.addstr(6, 42, '" ' * 10)

def draw_status(s):
    s.addstr(1, 2, '{:76s}'.format(status[:76]), curses.A_REVERSE)

def draw_display(s):
    s.addstr(2, 2, '{:76s}'.format(display[:76]), curses.A_REVERSE)

def draw_controls(s):
    s.clear()
    s.border('|','|','-','-','.','.','\'','\'')
    draw_status(s)
    draw_display(s)
    draw_progress(s)
    draw_integrity(s)
    draw_power(s)
    draw_turbo(s)
    draw_field(s)
    draw_river(s)
    draw_adrive(s)
    draw_xreg(s)
    draw_gibson(s)
    draw_security(s)
    draw_writeprotect(s)
    draw_baud(s)
    draw_editor(s)
    draw_cipher(s)
    draw_defrag(s)
    draw_irq(s)
    draw_snake(s)
    draw_java(s)
    draw_modem(s)
    draw_logic(s)

def redraw(name, s):
    if name == "power": draw_power(s)
    elif name == "turbo": draw_turbo(s)
    elif name == "field": draw_field(s)
    elif name == "river": draw_river(s)
    elif name == "adrive": draw_adrive(s)
    elif name == "xreg": draw_xreg(s)
    elif name == "gibson":
        draw_gibson(s)
        draw_security(s)
    elif name == "security": draw_security(s)
    elif name == "writeprotect": draw_writeprotect(s)
    elif name == "baud": draw_baud(s)
    elif name == "editor": draw_editor(s)
    elif name == "cipher": draw_cipher(s)
    elif name == "defrag": draw_defrag(s)
    elif name == "irq": draw_irq(s)
    elif name == "snake": pass
    elif name == "java": draw_java(s)
    elif name == "modem": draw_modem(s)
    elif name == "logic": draw_logic(s)

keys = {
    'a': "adrive",
    'b': "baud",
    'c': "cipher",
    'd': "defrag",
    'e': "editor",
    'f': "field",
    'g': "gibson",
    'i': "irq",
    'j': "java",
    'l': "logic",
    'p': "power",
    'r': "river",
    's': "security",
    't': "turbo",
    'w': "writeprotect",
    'x': "xreg",
    '0': "modem",
    '1': "modem",
    '2': "modem",
    '3': "modem",
    '4': "modem",
    '5': "modem",
    '6': "modem",
    '7': "modem",
    '8': "modem",
    '9': "modem",
}

def main(stdscr):
    global controls
    global display
    global status
    global progress
    global integrity
    status_timer = 0
    signal.signal(signal.SIGTSTP, signal.SIG_IGN)
    signal.signal(signal.SIGTTIN, signal.SIG_IGN)
    signal.signal(signal.SIGTTOU, signal.SIG_IGN)
    stdscr.nodelay(True)
    curses.noecho()
    curses.cbreak()
    stdscr.keypad(True)
    draw_controls(stdscr)
    stdscr.move(0, 0)
    client = Client('0.0.0.0')
    try:
        client.start({"controls": [{"id": id,
                       "state": controls[id]["state"],
                       "actions": controls[id]["actions"]}
                      for id in controls]})
    except socket.timeout:
        sys.exit(1)
    while client.running():
        try:
            while True:
                inst = client.get_instruction()
                if inst is None: break
                if inst['type'] == 'display':
                    display = inst['message']
                    draw_display(stdscr)
                elif inst['type'] == 'status':
                    status = inst['message']
                    status_timer = 100
                    draw_status(stdscr)
                elif inst['type'] == 'progress':
                    progress = int(inst['message'])
                    draw_progress(stdscr)
                elif inst['type'] == 'integrity':
                    integrity = int(inst['message'])
                    draw_integrity(stdscr)
            start_state = {id: controls[id]["state"] for id in controls}
            c = stdscr.getch()
            if 0 <= c <= 255 and chr(c).lower() in keys:
                control_name = keys[chr(c).lower()]
                activate(control_name, chr(c))
                redraw(control_name, stdscr)
            if c == curses.KEY_UP: drive_snake(0, -1)
            elif c == curses.KEY_DOWN: drive_snake(0, 1)
            elif c == curses.KEY_LEFT: drive_snake(-1, 0)
            elif c == curses.KEY_RIGHT: drive_snake(+1, 0)
            for name in controls:
                if "timer" in controls[name]:
                    if controls[name]["timer"] > 0:
                        controls[name]["timer"] -= 1
                        if controls[name]["timer"] == 0:
                            if name == "modem":
                                controls[name]["number"] = ""
                            if name == "java":
                                if controls[name]["state"] == "start1":
                                    controls[name]["state"] = "start2"
                                elif controls[name]["state"] == "start2":
                                    controls[name]["state"] = "start3"
                                elif controls[name]["state"] == "start3":
                                    controls[name]["state"] = "run"
                                elif controls[name]["state"] == "run":
                                    controls[name]["state"] = "0"
                                controls[name]["timer"] = 30
                            else:
                                controls[name]["state"] = controls[name]["off"]
                            if name == "defrag":
                                controls[name]["drive"] = fragments()
                            redraw(name, stdscr)
            if status_timer > 0:
                status_timer -= 1
                if status_timer == 0:
                    status = ""
                    draw_status(stdscr)
            update_snake(stdscr)
            stdscr.move(0,0)
            end_state = {id: controls[id]["state"] for id in controls}
            for id in controls:
                if start_state[id] != end_state[id]:
                    client.update(id, end_state[id])
            time.sleep(0.1)
	except KeyboardInterrupt:
	    stdscr.redrawwin()
	    stdscr.refresh()
    client.stop()

wrapper(main)
