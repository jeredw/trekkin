#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fb.h"

struct fb fb;

static int file;

int fb_init() {
  file = open("/dev/fb0", O_RDWR);
  if (file <= 0) {
    perror("failed to open /dev/fb0");
    goto error;
  }
  struct fb_var_screeninfo info;
  if (ioctl(file, FBIOGET_VSCREENINFO, &info) != 0) {
    perror("fb ioctl failed");
    goto error;
  }
  fb.width = info.xres;
  fb.pitch = 4 * info.xres;
  fb.height = info.yres;
  if (info.bits_per_pixel != 32) {
    fprintf(stderr, "fb bits_per_pixel is not 32");
    goto error;
  }
  if (info.transp.offset != 24 ||
      info.red.offset != 16 ||
      info.green.offset != 8 ||
      info.blue.offset != 0) {
    fprintf(stderr, "fb pixel format is not ARGB");
    goto error;
  }
  size_t len = fb.pitch * fb.height;
  fb.mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
  if (fb.mem == MAP_FAILED) {
    fb.mem = 0;
    perror("fb mmap failed");
    goto error;
  }
  return 0;

error:
  fb_shutdown();
  return -1;
}

void fb_shutdown() {
  if (file > 0) {
    close(file);
  }
  if (fb.mem != 0) {
    size_t len = fb.pitch * fb.height;
    munmap(fb.mem, len);
  }
}
