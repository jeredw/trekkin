#pragma once

namespace trek {

#define SOUND_LIST                                    \
  SOUND(JOIN, "art/laser.wav"),                       \
      SOUND(START_MISSION, "art/engine_startup.wav"), \
      SOUND(GAME_OVER, "art/slowblip.wav"),           \
      SOUND(GOOD_COMMAND, "art/kablip.wav"),          \
      SOUND(BAD_COMMAND, "art/whoosh.wav"), SOUND(ALARM, "art/alarm.wav"), \
      SOUND(CONTRA, "art/contra.wav"),

#define SOUND(id, path) id##_SOUND
enum Sound { SOUND_LIST NUM_SOUNDS };
#undef SOUND
#define SOUND(id, path) path
const char* const SFX_PATH[] = {SOUND_LIST};
#undef SOUND
#undef SOUNDS

void init_audio();
void cleanup_audio();
void play_music();
void play_sound(Sound sound);
}
