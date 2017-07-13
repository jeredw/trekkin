#ifndef FB_H
#define FB_H

typedef unsigned int u32;

struct fb {
  u32* mem;
  int pitch;
  int width;
  int height;
};

int fb_init();
void fb_shutdown();

#endif
