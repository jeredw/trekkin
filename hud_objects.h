#pragma once

const float ATLAS_WIDTH = 640;
const float ATLAS_HEIGHT = 480;

struct HudObject {
  HudObject(int image_x0, int image_y0, int image_x1, int image_y1) {
    float x0 = (float)image_x0;
    float y0 = (float)image_y0;
    float x1 = (float)image_x1;
    float y1 = (float)image_y1;
    width = x1 - x0;
    height = y1 - y0;
    s0 = x0 / ATLAS_WIDTH;
    t0 = y0 / ATLAS_HEIGHT;
    s1 = x1 / ATLAS_WIDTH;
    t1 = y1 / ATLAS_HEIGHT;
  }

  float width;
  float height;
  float s0;
  float t0;
  float s1;
  float t1;
};

static HudObject TITLE = HudObject(14, 133, 581, 264);
static HudObject HULL_MESSAGE[] = {
    HudObject(495, 21, 606, 127), HudObject(379, 21, 490, 127),
    HudObject(262, 21, 373, 127), HudObject(143, 21, 254, 127),
    HudObject(23, 21, 134, 127),
};
static HudObject GRAY_PANEL = HudObject(21, 276, 156, 341);
static HudObject ACTIVE_PANELS[] = {
    HudObject(160, 276, 295, 341), HudObject(21, 344, 156, 409),
    HudObject(160, 344, 295, 409), HudObject(21, 412, 156, 477),
};
static HudObject HULL_MAP[] = {
    HudObject(417, 433, 639, 470), HudObject(417, 393, 639, 430),
    HudObject(417, 353, 639, 390), HudObject(417, 312, 639, 349),
    HudObject(408, 272, 639, 309),
};
