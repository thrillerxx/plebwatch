#pragma once

#include <stdint.h>

constexpr uint32_t PLEB_STEPS_GOAL = 5000;

// Init IMU pedometer state; rolls the day counter when the local date changes.
void plebStepsBegin();

// Sample accelerometer for windowMs and accrue steps (call while awake or on micro-wake).
void plebStepsSampleWindow(uint32_t windowMs);

// One poll from the main loop (~50 Hz when delay(20)).
void plebStepsPoll();

uint32_t plebStepsToday();
uint32_t plebStepsGoal();
uint32_t plebStepsDayKey();

// True once when today's count first crosses the goal (latched until next day).
bool plebStepsConsumeGoalHit();
