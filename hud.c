#include <stdio.h>
#include "fb.h"

extern struct fb fb;

#define NUM_STARS 1000
struct star {
  int x;
  int y;
  int xv;
  int yv;
} stars[NUM_STARS];

static void clear() {
  memset(fb.mem, 0, fb.pitch * fb.height);
}
static void cursor_off() {
  printf("\e[?25l");
  fflush(stdout);
}
static void cursor_on() {
  printf("\e[?25h");
  fflush(stdout);
}

static void stars_reset(struct star* s) {
  s->x = fb.width / 2;
  s->y = fb.height / 2;
  s->xv = 5 - (rand() % 10);
  s->yv = 5 - (rand() % 10);
}

static void stars_init() {
  for (int i = 0; i < NUM_STARS; i++) {
    stars_reset(&stars[i]);
  }
}

static void stars_update() {
  for (int i = 0; i < NUM_STARS; i++) {
    struct star* s = &stars[i];
    fb.mem[s->y * fb.width + s->x] = 0;
    s->y += s->yv;
    s->x += s->xv;
    if (s->y < 0 || s->y >= fb.height || s->x < 0 || s->x >= fb.width) {
      stars_reset(s);
    }
    fb.mem[s->y * fb.width + s->x] = 0x00ffffff;
  }
}

int main() {
  if (fb_init()) {
    return 1;
  }
  cursor_off();
  clear();
  stars_init();
  while (1) {
    stars_update();
    usleep(10000);
  }
  cursor_on();
  fb_shutdown();
  return 0;
}
