#pragma once

#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "inc/gl.h"
#include "inc/vec.h"

#define f32 GLfloat

struct asteroid {
	vec vel, start_pos, impact_pos;
	f32 rot, scale;
	uint64_t gen_time;
	uint64_t impact_time;
};

struct player {
	uint8_t valid;
	vec vel, pos;
	struct sockaddr_in address;
	uint64_t client_index;
};

struct current_state {
	struct asteroid *asteroids;
	uint16_t max_asteroid_id;
	struct player *players;
	uint8_t max_player_id;
	f32 sun_pos;
	uint64_t damage;
	uint8_t *damage_index;
	uint64_t epoch;
	uint64_t state_index;
	uint32_t lock;

	pid_t world_thread;
	pid_t asteroid_thread;
	pid_t player_thread;

	uint8_t *world_stack;
	uint8_t *asteroid_stack;
	uint8_t *player_stack;

	uint64_t last_player_update;
};

struct state_holder {
	uint64_t epoch;
	uint16_t max_asteroid_id;
	struct current_state *state;
};

struct client_holder {
	struct sockaddr_in address;
	uint64_t epoch;
	uint64_t last_recvd;
	uint8_t valid;
	uint64_t state_index;
	uint8_t player_id;
};
