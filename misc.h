#pragma once

#define ARRAYSIZE(xs) (sizeof(xs) / sizeof(xs[0]))
#define CHOOSE(xs) xs[rand() % ARRAYSIZE(xs)]
