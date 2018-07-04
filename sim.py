#!/usr/bin/env python
import random

class Command(object):
  def __init__(self, target, assigned_tick, duration, deadline):
    self.target = target
    self.assigned_tick = assigned_tick
    self.duration = duration
    self.deadline = deadline

class Panel(object):
  def __init__(self, name, mean_ticks_per_action, stdev_ticks_per_action):
    self.name = name
    self.mean_ticks_per_action = mean_ticks_per_action 
    self.stdev_ticks_per_action = stdev_ticks_per_action
    self.shown_command = None
    self.self_targetted = 0
    self.targetted = 0
    self.missed_commands = 0
    self.successful_commands = 0
    self.last_targetted_tick = 0
    self.idle_ticks = 0

panels = [
  Panel(name = 'Alien lab',
        mean_ticks_per_action = 12,
        stdev_ticks_per_action = 4),
  Panel(name = 'Apple 2',
        mean_ticks_per_action = 8,
        stdev_ticks_per_action = 4),
  Panel(name = 'Navigation',
        mean_ticks_per_action = 8,
        stdev_ticks_per_action = 3),
]

def deadline(game_time):
  return (20 if game_time < 100 else
          10 if game_time < 300 else
          5)

def summarize(xs):
  mean = sum(xs) / float(len(xs))
  stdev = sum((x - mean) * (x - mean) for x in xs) / float(len(xs))
  return '{} s.d. {}'.format(mean, stdev)

def lru(panels):
  cost = {p.name: (p.last_targetted_tick, random.random()) for p in panels}
  return min(panels, key = lambda p: cost[p.name])

def simulate(panels, iter = 1000, pick_target = random.choice, deadline = deadline):
  all_game_times = []
  all_missed_commands = []
  all_successful_commands = []
  tick = 0
  for i in xrange(iter):
    missed_commands = 0
    successful_commands = 0
    game_time = 0
    while missed_commands - successful_commands / 3 <= 5:
      tick += 1
      game_time += 1
      for p in panels:
        if not p.shown_command:
          if iter == 1:
	    for q in panels:
	      print("- {}: {}".format(q.name, q.last_targetted_tick))
          target = pick_target(panels)
          target.last_targetted_tick = tick
          target.targetted += 1
          if target == p:
            target.self_targetted += 1
          duration = random.gauss(target.mean_ticks_per_action,
                                  target.stdev_ticks_per_action)
          if duration < 1: duration = 1
          p.shown_command = Command(target = target,
                                    assigned_tick = tick,
                                    duration = duration,
                                    deadline = tick + deadline(game_time))
          if iter == 1:
            print("{},{},{}".format(tick, p.name, target.name))
        else:
          if tick >= p.shown_command.deadline:
            if iter == 1:
              print("{} {} missed".format(tick, p.shown_command.target.name))
            missed_commands += 1
            p.shown_command.target.missed_commands += 1
            p.shown_command = None
          elif tick - p.shown_command.assigned_tick >= p.shown_command.duration:
            if iter == 1:
              print("{} {} succeeded".format(tick, p.shown_command.target.name))
            successful_commands += 1
            p.shown_command.target.successful_commands += 1
            p.shown_command = None
      targets = [p.shown_command.target for p in panels if p.shown_command]
      for p in panels:
        if not p in targets:
          p.idle_ticks += 1
    for p in panels:
      p.shown_command = None
    all_game_times.append(game_time)
    all_missed_commands.append(missed_commands)
    all_successful_commands.append(successful_commands)
  return all_game_times, all_missed_commands, all_successful_commands

game_times, missed_commands, successful_commands = simulate(panels, pick_target = lru)
#game_times, missed_commands, successful_commands = simulate(panels, pick_target = random.choice)
print "game_times: {}".format(summarize(game_times))
print "missed_commands: {}".format(summarize(missed_commands))
print "successful_commands: {}".format(summarize(successful_commands))
assignments = sum(p.targetted for p in panels)
for p in panels:
  print "panel {}:".format(p.name)
  print "- self_targetted%: {}".format(100.0 * p.self_targetted / float(p.targetted))
  print "- targetted%: {}".format(100.0 * p.targetted / float(assignments))
  print "- idle%: {}".format(100.0 * p.idle_ticks / float(sum(game_times)))
  print "- miss%: {}".format(100.0 * p.missed_commands / float(p.successful_commands + p.missed_commands))
