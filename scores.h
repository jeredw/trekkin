#pragma once

#include <vector>
#include <algorithm>

namespace trek {

struct HighScore {
  HighScore() : game_number(0), score(0) {}
  HighScore(int g, int s) : game_number(g), score(s) {}
  int game_number;
  int score;
};

const char *const HIGH_SCORES_FILE = "high_scores.txt";
const int NUM_HIGH_SCORES = 10;

void get_high_scores(std::vector<HighScore> *scores);
void add_high_score(HighScore new_score);
}
