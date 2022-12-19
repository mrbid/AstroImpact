#pragma once

#include <stdint.h>

#include "types.h"
#include "inc/vec.h"

uint64_t microtime(void);
uint8_t do_exo_impact(struct current_state *state, vec pos, float f);
void rand_asteroid(struct asteroid *asteroid, uint64_t timestamp);
uint64_t min_asteroid_time(void);
void rand_all_asteroids(struct current_state *state, uint64_t timestamp);
