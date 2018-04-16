#include <stdio.h>
#include <unistd.h>

#include "scores.h"

namespace trek {

void get_high_scores(std::vector<HighScore>* scores) {
  scores->clear();
  FILE* fp = fopen(HIGH_SCORES_FILE, "rb");
  if (fp == nullptr) {
    return;
  }
  HighScore next;
  while (fscanf(fp, "%d %d\n", &next.game_number, &next.score) == 2) {
    scores->push_back(next);
  }
  fclose(fp);
}

void add_high_score(HighScore new_score) {
  std::vector<HighScore> scores;
  get_high_scores(&scores);
  scores.push_back(new_score);
  std::sort(scores.begin(), scores.end(),
            [](const HighScore& a,
               const HighScore& b) { return a.score >= b.score; });

  // so the display process doesn't see a partial score list
  char temp_file[] = "/tmp/trekXXXXXX";
  int fd = mkstemp(temp_file);
  if (fd == -1) {
    return;
  }
  FILE* fp = fdopen(fd, "wb");
  if (fp == nullptr) {
    close(fd);
    unlink(temp_file);
    return;
  }
  for (int i = 0; i < std::min((int)scores.size(), NUM_HIGH_SCORES); i++) {
    fprintf(fp, "%d %d\n", scores[i].game_number, scores[i].score);
  }
  fsync(fd);
  fclose(fp);
  rename(temp_file, HIGH_SCORES_FILE);
}
}
