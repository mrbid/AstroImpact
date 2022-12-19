#include <sys/time.h>
#include <stdint.h>

#include <stdio.h>

#include "main.h"
#include "utils.h"
#include "types.h"
#include "assets/models.h"
#include "inc/vec.h"
#include "config.h"

uint64_t microtime(void) {
	struct timeval tv;
	struct timezone tz = {0};

	gettimeofday(&tv, &tz);
	return (tv.tv_sec * 1000000) + tv.tv_usec;
}

uint8_t do_exo_impact(struct current_state *state, vec pos, float f) {
	for (GLsizeiptr i = 0; i < exo_numvert; i++) {
		if (!state->damage_index[i]) {
			vec v = {exo_vertices[i*3], exo_vertices[(i*3)+1], exo_vertices[(i*3)+2]};
			if (vDistSq(v, pos) < f*f) {
				state->damage++;
				state->damage_index[i] = 1;
			}
		}
	}

	return state->damage >= (exo_numvert/2);
}

//void rand_asteroid(struct asteroid *asteroid) {
//	vRuvBT(&(asteroid->pos));
//	vMulS(&(asteroid->pos), asteroid->pos, 10.f);
//
//	vec dp;
//	vRuvTA(&dp);
//	vSub(&(asteroid->vel), asteroid->pos, dp);
//	vNorm(&(asteroid->vel));
//	vInv(&(asteroid->vel));
//
//	vec of = asteroid->vel;
//	vInv(&of);
//	vMulS(&of, of, randf() * 6.f);
//	vAdd(&(asteroid->pos), asteroid->pos, of);
//
//	asteroid->rot = randf() * 300.f;
//	asteroid->scale = 0.01f + (randf() * 0.07f);
//	f32 speed = 0.16f + (randf() * 0.08f);
//	asteroid->vel.x = asteroid->vel.x * speed;
//	asteroid->vel.y = asteroid->vel.y * speed;
//	asteroid->vel.z = asteroid->vel.z * speed;
//}

void rand_asteroid(struct asteroid *asteroid, uint64_t timestamp) {
	asteroid->gen_time = timestamp;

	asteroid->rot = randf() * 300.f;
	asteroid->scale = 0.01f + (randf() * 0.07f);
	f32 speed = 0.16f + (randf() * 0.08f);

	vRuv(&asteroid->start_pos);
	while (vSum(asteroid->start_pos) == 0.f) // can't use null vectors
		vRuv(&asteroid->start_pos);

	vNorm(&asteroid->start_pos);

	f32 dist = randfc()+10.f;
	asteroid->start_pos.x *= dist;
	asteroid->start_pos.y *= dist;
	asteroid->start_pos.z *= dist;

	vec impactpos;
	vRuv(&impactpos);
	while (vSum(impactpos) == 0.f)
		vRuv(&impactpos);

	vNorm(&impactpos);
	impactpos.x *= EXO_TARGET_RADIUS;
	impactpos.y *= EXO_TARGET_RADIUS;
	impactpos.z *= EXO_TARGET_RADIUS;

	asteroid->vel = (vec){impactpos.x - asteroid->start_pos.x, impactpos.y - asteroid->start_pos.y, impactpos.z - asteroid->start_pos.z};

	vNorm(&asteroid->vel);

	f32 a = vMag(asteroid->vel);
	f32 b = 2.f*vDot(asteroid->vel, asteroid->start_pos);
	f32 c = vMag(asteroid->start_pos) - (EXO_IMPACT_RADIUS * EXO_IMPACT_RADIUS);

	f32 dis = b*b - 4.f*a*c;
	if (dis < 0.f) // safety, it *will* impact, but it might just scrape the edge (and float inaccuracies result in a negative number)
		dis = 0.f;

	f32 dist_to_impact = (-b - sqrtps(dis))/(2.f*a); // negative root for near end
	asteroid->impact_pos = (vec){
		asteroid->start_pos.x + asteroid->vel.x*dist_to_impact,
		asteroid->start_pos.y + asteroid->vel.y*dist_to_impact,
		asteroid->start_pos.z + asteroid->vel.z*dist_to_impact
	};

	asteroid->vel.x *= speed;
	asteroid->vel.y *= speed;
	asteroid->vel.z *= speed;

	speed = vMod(asteroid->vel);
	asteroid->impact_time = timestamp + (uint64_t)(dist_to_impact * 1000000.f / speed);
}

uint64_t min_asteroid_time(void) {
	f32 speed = 0.24f;

	vec pos = {1.f,0.f,0.f};

	vNorm(&pos);

	f32 dist = 9.f;
	pos.x *= dist;
	pos.y *= dist;
	pos.z *= dist;

	vec impactpos = {1.f,0.f,0.f};

	vNorm(&impactpos);
	impactpos.x *= EXO_TARGET_RADIUS;
	impactpos.y *= EXO_TARGET_RADIUS;
	impactpos.z *= EXO_TARGET_RADIUS;

	vec vel = {impactpos.x - pos.x, impactpos.y - pos.y, impactpos.z - pos.z};

	vNorm(&vel);

	f32 a = vMag(vel);
	f32 b = 2.f*vDot(vel, pos);
	f32 c = vMag(pos) - (EXO_IMPACT_RADIUS * EXO_IMPACT_RADIUS);

	f32 dis = b*b - 4.f*a*c;
	if (dis < 0.f) // safety, it *will* impact, but it might just scrape the edge (and float inaccuracies result in a negative number)
		dis = 0.f;

	f32 dist_to_impact = (-b - sqrtps(dis))/(2.f*a); // negative root for near end

	vel.x *= speed;
	vel.y *= speed;
	vel.z *= speed;

	speed = vMod(vel);

	return (uint64_t)(dist_to_impact * 1000000.f / speed);
}

void rand_all_asteroids(struct current_state *state, uint64_t timestamp) {
	for (uint32_t i = 0; i <= (uint32_t)state->max_asteroid_id; i++) {
		rand_asteroid(&(state->asteroids[i]), timestamp);
	}
}
