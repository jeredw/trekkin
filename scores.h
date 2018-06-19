#pragma once

#include <vector>
#include <string>
#include <algorithm>

namespace trek {

struct HighScore {
  HighScore() {}
  HighScore(int g, std::string i, int s) : game_number(g), initials(i), score(s) {}
  int game_number = 0;
  std::string initials{"???"};
  int score = 0;
};

const char *const HIGH_SCORES_FILE = "high_scores.txt";
const int NUM_HIGH_SCORES = 10;

void get_high_scores(std::vector<HighScore> *scores);
void add_high_score(HighScore new_score);
}
