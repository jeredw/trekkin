#pragma once

const float ATLAS_WIDTH = 640;
const float ATLAS_HEIGHT = 480;

struct HudObject {
  HudObject(int image_x0, int image_y0, int image_x1, int image_y1) {
    src_x0 = (float)image_x0;
    src_y0 = (float)image_y0;
    src_x1 = (float)image_x1;
    src_y1 = (float)image_y1;
    s0 = src_x0 / ATLAS_WIDTH;
    t0 = src_y0 / ATLAS_HEIGHT;
    s1 = src_x1 / ATLAS_WIDTH;
    t1 = src_y1 / ATLAS_HEIGHT;
  }

  // position in atlas
  float src_x0;
  float src_y0;
  float src_x1;
  float src_y1;
  float s0;
  float t0;
  float s1;
  float t1;

  // position on screen
  float x0;
  float y0;
  float x1;
  float y1;
};

static HudObject TITLE = HudObject(14, 133, 581, 264);
static HudObject SHIP_100 = HudObject(23, 21, 134, 127);
static HudObject SHIP_80 = HudObject(143, 21, 254, 127);
static HudObject SHIP_60 = HudObject(262, 21, 373, 127);
static HudObject SHIP_40 = HudObject(379, 21, 490, 127);
static HudObject SHIP_20 = HudObject(495, 21, 606, 127);
static HudObject PANEL_GRAY = HudObject(21, 276, 156, 341);
static HudObject PANEL_BLUE = HudObject(160, 276, 295, 341);
static HudObject PANEL_ORANGE = HudObject(21, 344, 156, 409);
static HudObject PANEL_GREEN = HudObject(160, 344, 295, 409);
static HudObject PANEL_PURPLE = HudObject(21, 412, 156, 477);
static HudObject HULL_100 = HudObject(408, 272, 639, 309);
static HudObject HULL_80 = HudObject(417, 312, 639, 349);
static HudObject HULL_60 = HudObject(417, 353, 639, 390);
static HudObject HULL_40 = HudObject(417, 393, 639, 430);
static HudObject HULL_20 = HudObject(417, 433, 639, 470);
