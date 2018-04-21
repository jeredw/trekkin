#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "audio.h"
#include "log.h"
#define ALOG(...) log("A", __VA_ARGS__)

extern bool sfx;

namespace trek {

const char *const GROOVE_PATH = "art/groove.mp3";
static Mix_Music *groove;
static Mix_Chunk *sounds[NUM_SOUNDS];

void init_audio() {
  if (!sfx) {
    return;
  }
  if (SDL_Init(SDL_INIT_AUDIO) == -1) {
    ALOG("couldn't init SDL: %s", SDL_GetError());
    sfx = false;
    return;
  }
  if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) == -1) {
    ALOG("couldn't open mixer: %s", Mix_GetError());
    sfx = false;
    return;
  }
  if ((groove = Mix_LoadMUS(GROOVE_PATH)) == nullptr) {
    ALOG("couldn't load music: %s", Mix_GetError());
    sfx = false;
    return;
  }
  for (int i = 0; i < (int)NUM_SOUNDS; i++) {
    if ((sounds[i] = Mix_LoadWAV(SFX_PATH[i])) == nullptr) {
      ALOG("couldn't load sound effect %s: %s", SFX_PATH[i], Mix_GetError());
      sfx = false;
      return;
    }
  }
}

void play_sound(Sound sound) {
  if (sfx && sounds[sound] != nullptr) {
    Mix_PlayChannel(-1, sounds[sound], 0);
  }
}

void play_music() {
  if (sfx && groove != nullptr) {
    Mix_PlayMusic(groove, -1);
  }
}

void cleanup_audio() {
  for (int i = 0; i < (int)NUM_SOUNDS; i++) {
    if (sounds[i] != nullptr) {
      Mix_FreeChunk(sounds[i]);
    }
  }
  if (groove != nullptr) {
    Mix_FreeMusic(groove);
  }
  if (sfx) {
    SDL_Quit();
  }
}
}
