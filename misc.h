#pragma once

#define ARRAYSIZE(xs) (sizeof(xs) / sizeof(xs[0]))
#define CHOOSE(xs) xs[rand() % ARRAYSIZE(xs)]

template <typename T>
inline T clamp(T value, T min, T max) {
  if (value < min) {
    value = min;
  } else if (value > max) {
    value = max;
  }
  return value;
}
