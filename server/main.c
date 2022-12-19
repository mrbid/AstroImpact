#define _GNU_SOURCE
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <endian.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sched.h>

#include "main.h"
#include "types.h"
#include "utils.h"
#include "inc/protocol.h"

#define EXO_VERTICES_ONLY
#include "assets/models.h"

#include "config.h"

#include "inc/mutex.h"
#include "inc/munmap.h"

int fd;

uint32_t main_lock = 0;
struct state_holder *states = 0;
uint64_t num_states = 0;

struct client_holder *all_clients = 0;
uint64_t num_clients = 0;
uint64_t min_time_to_impact;

int main_loop_world(void *tmp) {
	struct current_state *state = tmp; // annoying type checking
	char *my_stack = state->world_stack;

	{
		uint64_t t = state->epoch - 1;
		for (uint64_t t1 = time(0); t1 < t; t1 = time(0)) // for loop abuse
			sleep(t - t1);

		t = state->epoch * 1000000;
		for (uint64_t t1 = microtime(); t1 < t; t1 = microtime())
			usleep(t - t1);
	}

	printf("Epoch reached! Epoch: %ld, Current time: %ld\n(microepoch: %ld, microtime: %ld)\n",
			state->epoch, time(0), state->epoch * 1000000, microtime());

	rand_all_asteroids(state, state->epoch * 1000000);

	uint8_t buffer[MAX_PACKET_SIZE];
	*((uint64_t*)buffer) = PROTOCOL_ID;
	buffer[8] = MSG_TYPE_EXO_HIT; // only message we'll want to send from here

	uint64_t last_time = microtime();
	uint16_t hit_id = 0; // arbitrary number
	mutex_lock(&(state->lock));
	while (1) {
		uint16_t asteroid_id = 0;
		uint64_t soonest = state->asteroids[0].impact_time;
		for (uint32_t i = 1; i <= (uint32_t)state->max_asteroid_id; i++) {
			if (state->asteroids[i].impact_time < soonest) {
				asteroid_id = i;
				soonest = state->asteroids[i].impact_time;
			}
		}

		if (soonest <= microtime()) {
			struct asteroid *asteroid = &(state->asteroids[asteroid_id]);
			uint64_t current_time = asteroid->impact_time;
			f32 time_delta = (f32)(current_time - last_time) * 0.000001f;
			last_time = asteroid->impact_time;

			uint8_t end_game = do_exo_impact(state, asteroid->impact_pos, (asteroid->scale+(vMod(asteroid->vel)*0.1f))*1.2f);

			*((uint64_t*)(buffer+9)) = HTOLE64(current_time);
			*((uint16_t*)(buffer+17)) = HTOLE16(hit_id);
			*((uint16_t*)(buffer+19)) = HTOLE16(asteroid_id);
			*((f32*)(buffer+21)) = HTOLE32F(asteroid->vel.x);
			*((f32*)(buffer+25)) = HTOLE32F(asteroid->vel.y);
			*((f32*)(buffer+29)) = HTOLE32F(asteroid->vel.z);
			*((f32*)(buffer+33)) = HTOLE32F(asteroid->impact_pos.x);
			*((f32*)(buffer+37)) = HTOLE32F(asteroid->impact_pos.y);
			*((f32*)(buffer+41)) = HTOLE32F(asteroid->impact_pos.z);
			// @+45
			// 2 more vecs, +24
			*((f32*)(buffer+69)) = HTOLE32F(asteroid->rot);
			*((f32*)(buffer+73)) = HTOLE32F(asteroid->scale);

			rand_asteroid(asteroid, current_time);

			//+45 to +64
			*((f32*)(buffer+45)) = HTOLE32F(asteroid->vel.x);
			*((f32*)(buffer+49)) = HTOLE32F(asteroid->vel.y);
			*((f32*)(buffer+53)) = HTOLE32F(asteroid->vel.z);
			*((f32*)(buffer+57)) = HTOLE32F(asteroid->start_pos.x);
			*((f32*)(buffer+61)) = HTOLE32F(asteroid->start_pos.y);
			*((f32*)(buffer+65)) = HTOLE32F(asteroid->start_pos.z);
			// @+69
			// 2 f32s, +8
			*((f32*)(buffer+77)) = HTOLE32F(asteroid->rot);
			*((f32*)(buffer+81)) = HTOLE32F(asteroid->scale);
			if (end_game)
				*((f32*)(buffer+85)) = HTOLE32F(20.f);
			else
				*((f32*)(buffer+85)) = HTOLE32F((f32)state->damage / (exo_numvert/2));
			// total size: 89

			for (uint16_t pid = 0; pid <= (uint16_t)state->max_player_id; pid++)
				if (state->players[pid].valid)
					sendto(fd, buffer, MSG_SIZE_EXO_HIT, 0, (struct sockaddr*)&state->players[pid].address, sizeof(struct sockaddr));

			hit_id++;

			if (end_game) {
				mutex_unlock(&(state->lock));
				mutex_lock(&main_lock);
				mutex_lock(&(state->lock)); // ordering to avoid deadlocks

				kill(state->player_thread, SIGKILL);
				munmap(state->player_stack, TOTAL_STACK_SIZE);
				kill(state->asteroid_thread, SIGKILL);
				munmap(state->asteroid_stack, TOTAL_STACK_SIZE);
				// don't kill self :P

				for (uint16_t pid = 0; pid <= (uint16_t)state->max_player_id; pid++)
					if (state->players[pid].valid)
						all_clients[state->players[pid].client_index].valid = 0;

				printf("World %lu finished.\n", state->epoch);
				states[state->state_index].epoch = 0;

				free(state->players);
				free(state->asteroids);
				free(state->damage_index);
				free(state);

				mutex_unlock(&main_lock);

				// signals between unmapping and exiting will segfault and kill the main thread, don't listen to them
				sigset_t sigset;
				sigfillset(&sigset);
				sigprocmask(SIG_SETMASK, &sigset, 0);

				munmap_and_exit(my_stack, TOTAL_STACK_SIZE);
			}
		} else {
			mutex_unlock(&(state->lock));

			{
				uint64_t min_time = min_time_to_impact+microtime();
				if (soonest > min_time)
					soonest = min_time;
			}

			for (uint64_t t1 = microtime(); t1 < (soonest - 1000000); t1 = microtime())
				sleep((soonest - t1) / 1000000);

			for (uint64_t t1 = microtime(); t1 < soonest; t1 = microtime())
				usleep(soonest - t1);

			mutex_lock(&(state->lock));
		}
	}

	return 0;
}

int main_loop_send_asteroids(void *tmp) {
	struct current_state *state = tmp; // annoying type checking

	uint64_t current_time;
	uint64_t last_time = microtime();

	uint8_t buffer[MAX_PACKET_SIZE];
	*((uint64_t*)buffer) = PROTOCOL_ID;
	while (1) {
		current_time = microtime();
		mutex_lock(&(state->lock));

		buffer[8] = MSG_TYPE_ASTEROID_POS;
		*((uint64_t*)(buffer+9)) = HTOLE64(current_time);
		uint16_t size = 17;
		for (uint32_t asteroid_id = 0; asteroid_id <= (uint32_t)state->max_asteroid_id; asteroid_id++) {
			f32 time_diff = (f32)(int64_t)(current_time - state->asteroids[asteroid_id].gen_time) * 0.000001f;

			vec pos = {
				state->asteroids[asteroid_id].start_pos.x + state->asteroids[asteroid_id].vel.x * time_diff,
				state->asteroids[asteroid_id].start_pos.y + state->asteroids[asteroid_id].vel.y * time_diff,
				state->asteroids[asteroid_id].start_pos.z + state->asteroids[asteroid_id].vel.z * time_diff
			};

			*((uint16_t*)(buffer+size)) = HTOLE16((uint16_t)asteroid_id);
			*((f32*)(buffer+size+2)) = HTOLE32F(state->asteroids[asteroid_id].vel.x);
			*((f32*)(buffer+size+6)) = HTOLE32F(state->asteroids[asteroid_id].vel.y);
			*((f32*)(buffer+size+10)) = HTOLE32F(state->asteroids[asteroid_id].vel.z);
			*((f32*)(buffer+size+14)) = HTOLE32F(pos.x);
			*((f32*)(buffer+size+18)) = HTOLE32F(pos.y);
			*((f32*)(buffer+size+22)) = HTOLE32F(pos.z);
			*((f32*)(buffer+size+26)) = HTOLE32F(state->asteroids[asteroid_id].rot);
			*((f32*)(buffer+size+30)) = HTOLE32F(state->asteroids[asteroid_id].scale);
			size += 34;

			if (size+34 > MAX_PACKET_SIZE || (uint16_t)asteroid_id == state->max_asteroid_id) {
				for (uint16_t pid = 0; pid <= (uint16_t)state->max_player_id; pid++)
					if (state->players[pid].valid)
						sendto(fd, buffer, size, 0, (struct sockaddr*)&state->players[pid].address, sizeof(struct sockaddr));

				size = 17;
			}
		}

		state->sun_pos += (f32)(current_time - last_time) * 0.000001f * 0.03f;
		{
			int64_t tmp = (int64_t)(state->sun_pos / (PI * 4.f)); // only want whole numbers
			if (tmp > 0)
				state->sun_pos = state->sun_pos - (PI * 4.f * (f32)tmp);
		}

		buffer[8] = MSG_TYPE_SUN_POS;
		*((f32*)(buffer+17)) = HTOLE32F(state->sun_pos);
		for (uint16_t pid = 0; pid <= (uint16_t)state->max_player_id; pid++)
			if (state->players[pid].valid)
				sendto(fd, buffer, MSG_SIZE_SUN_POS, 0, (struct sockaddr*)&state->players[pid].address, sizeof(struct sockaddr));

		mutex_unlock(&(state->lock));
		last_time = current_time;
		register uint64_t t = microtime();
		if (t < last_time + ASTEROID_SEND_UTIME)
			usleep(last_time + ASTEROID_SEND_UTIME - t);
		else
			fprintf(stderr, "WARNING: Asteroid send took too long, skipping sleep! (%luus)\n", t-last_time);
	}

	return 0;
}

int main_loop_send_players(void *tmp) {
	struct current_state *state = tmp; // annoying type checking

	uint64_t current_time;
	uint64_t last_time = microtime();

	uint8_t buffer[MAX_PACKET_SIZE];
	*((uint64_t*)buffer) = PROTOCOL_ID;
	buffer[8] = MSG_TYPE_PLAYER_POS;
	while (1) {
		current_time = microtime();
		mutex_lock(&(state->lock));

		f32 scale = (f32)(current_time - last_time) * 0.000001f;

		*((uint64_t*)(buffer+9)) = HTOLE64(current_time);
		uint16_t size = 17;
		for (uint16_t player_id = 0; player_id <= (uint16_t)state->max_player_id; player_id++) {
			if (state->players[player_id].valid) {
				*((uint8_t*)(buffer+size)) = player_id;

				state->players[player_id].pos.x += state->players[player_id].vel.x * scale;
				state->players[player_id].pos.y += state->players[player_id].vel.y * scale;
				state->players[player_id].pos.z += state->players[player_id].vel.z * scale;

				*((f32*)(buffer+size+1)) = HTOLE32F(state->players[player_id].pos.x);
				*((f32*)(buffer+size+5)) = HTOLE32F(state->players[player_id].pos.y);
				*((f32*)(buffer+size+9)) = HTOLE32F(state->players[player_id].pos.z);
				*((f32*)(buffer+size+13)) = HTOLE32F(state->players[player_id].vel.x);
				*((f32*)(buffer+size+17)) = HTOLE32F(state->players[player_id].vel.y);
				*((f32*)(buffer+size+21)) = HTOLE32F(state->players[player_id].vel.z);
				size += 25;
			}

			if (size+25 > MAX_PACKET_SIZE || (uint8_t)player_id == state->max_player_id) {
				for (uint16_t pid = 0; pid <= (uint16_t)state->max_player_id; pid++)
					if (state->players[pid].valid)
						sendto(fd, buffer, size, 0, (struct sockaddr*)&state->players[pid].address, sizeof(struct sockaddr));

				size = 17;
			}
		}

		state->last_player_update = current_time;

		mutex_unlock(&(state->lock));
		last_time = current_time;
		register uint64_t t = microtime();
		if (t < last_time + PLAYER_SEND_UTIME)
			usleep(last_time + PLAYER_SEND_UTIME - t);
		else
			fprintf(stderr, "WARNING: Player send took too long, skipping sleep! (%luus)\n", t-last_time);
	}

	return 0;
}

#define GFX_SCALE 0.01f
void scaleBuffer(GLfloat* b, GLsizeiptr s) {
	for(GLsizeiptr i = 0; i < s; i++)
		b[i] *= GFX_SCALE * 1.025;
}

int main(int argc, char **argv) {
	{
		struct sigaction ignore_sigchld;
		ignore_sigchld.sa_handler = SIG_IGN;
		if (sigaction(SIGCHLD, &ignore_sigchld, 0)) {
			fputs("ERROR: Unable to setup signal handler!\n(this should never actually happen)\n", stderr);
			return 1;
		}
	}

	min_time_to_impact = min_asteroid_time();

	scaleBuffer(exo_vertices, exo_numvert*3);

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		fputs("ERROR: Unable to create socket!\n", stderr);
		return 1;
	}

	{ // no reason to keep `server` and `len` around
		struct sockaddr_in server;
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = INADDR_ANY;

		if (argc > 1) {
			uint32_t port;
			if (strlen(argv[1]) > 5 || sscanf(argv[1], "%d", &port) != 1 || port > 65535) {
				fprintf(stderr, "ERROR: Invalid syntax! Syntax: %s [port]\n", argv[0]);
				return 2;
			} else if (port == 0) {
				fputs("WARNING: Port 0 is automatically chosen by the OS; if you do not intend this, restart this program and select a different port.\n", stderr);
			}
			server.sin_port = HTOBE16((uint16_t)port);
		} else {
			server.sin_port = SERVER_PORT;
		}

		if (bind(fd, (struct sockaddr*)&server, sizeof(server)) != 0) {
			fputs("ERROR: Unable to bind socket!\n", stderr);
			return 1;
		}

		socklen_t len = sizeof(server);
		if (getsockname(fd, (struct sockaddr*)&server, &len) == 0)
			printf("Using port %d.\n", BE16TOH(server.sin_port));
	}

	uint8_t buffer[MAX_PACKET_SIZE];
	while (1) {
		// TODO: Resend stuff that should be reliable here

		struct sockaddr_in source;
		socklen_t source_len = sizeof(source);
		ssize_t msg_len = recvfrom(fd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&source, &source_len);
		uint8_t *addr = (uint8_t*)&source.sin_addr.s_addr;
		if (msg_len < MIN_PACKET_SIZE) {
			fputs("Packet too small!\n", stderr);
			continue;
		} else if (((uint64_t*)buffer)[0] != PROTOCOL_ID) {
			fputs("Invalid ID given!\n", stderr);
			continue;
		}

		uint64_t current_time = microtime();

		switch (buffer[8]) {
		case MSG_TYPE_PLAYER_POS:
			if (msg_len < 17+(4*6)) {
				fputs("Packet too small for its type!\n", stderr);
				break;
			}

			mutex_lock(&main_lock);

			uint64_t sent_time = LE64TOH(*((uint64_t*)(buffer+9)));

			for (uint64_t i = 0; i < num_clients; i++) {
				if (all_clients[i].address.sin_addr.s_addr == source.sin_addr.s_addr && all_clients[i].address.sin_port == source.sin_port && all_clients[i].valid) {
					mutex_lock(&(states[all_clients[i].state_index].state->lock));

					vec pos, vel;

					f32 scale = (f32)(int64_t)(states[all_clients[i].state_index].state->last_player_update - sent_time) * 0.000001f;

					pos.x = LE32FTOH(*((f32*)(buffer+17)));
					pos.y = LE32FTOH(*((f32*)(buffer+21)));
					pos.z = LE32FTOH(*((f32*)(buffer+25)));

					vel.x = LE32FTOH(*((f32*)(buffer+29)));
					vel.y = LE32FTOH(*((f32*)(buffer+33)));
					vel.z = LE32FTOH(*((f32*)(buffer+37)));

					pos.x += vel.x*scale;
					pos.y += vel.y*scale;
					pos.z += vel.z*scale;

					states[all_clients[i].state_index].state->players[all_clients[i].player_id].pos.x = pos.x;
					states[all_clients[i].state_index].state->players[all_clients[i].player_id].pos.y = pos.y;
					states[all_clients[i].state_index].state->players[all_clients[i].player_id].pos.z = pos.z;

					states[all_clients[i].state_index].state->players[all_clients[i].player_id].vel.x = vel.x;
					states[all_clients[i].state_index].state->players[all_clients[i].player_id].vel.y = vel.y;
					states[all_clients[i].state_index].state->players[all_clients[i].player_id].vel.z = vel.z;

					mutex_unlock(&(states[all_clients[i].state_index].state->lock));

					all_clients[i].last_recvd = current_time;
					break;
				}
			}

			mutex_unlock(&main_lock);
			break;
		case MSG_TYPE_ASTEROID_DESTROYED:
			if (msg_len < 19) {
				fputs("Packet too small for its type!\n", stderr);
				break;
			}

			mutex_lock(&main_lock);

			for (uint64_t i = 0; i < num_clients; i++) {
				if (all_clients[i].address.sin_addr.s_addr == source.sin_addr.s_addr && all_clients[i].address.sin_port == source.sin_port && all_clients[i].valid) {
					mutex_lock(&(states[all_clients[i].state_index].state->lock));

					all_clients[i].last_recvd = current_time;

					uint64_t microtimestamp = LE64TOH(*((uint64_t*)(buffer+9)));
					uint16_t asteroid_id = LE16TOH(*((uint16_t*)(buffer+17)));
					if (asteroid_id > states[all_clients[i].state_index].state->max_asteroid_id) {
						fputs("Client attempted to destroy invalid asteroid!\n", stderr);
						goto skip_asteroid_destroyed;
					}

					struct asteroid *asteroid = &(states[all_clients[i].state_index].state->asteroids[asteroid_id]);

					uint8_t send_all;
					if (states[all_clients[i].state_index].state->asteroids[asteroid_id].gen_time < microtimestamp) {
						rand_asteroid(asteroid, microtimestamp);
						send_all = 1;
					} else {
						send_all = 0;
					}

					buffer[8] = MSG_TYPE_ASTEROID_DESTROYED;
					*((uint64_t*)(buffer+9)) = HTOLE64(asteroid->gen_time);
					*((uint16_t*)(buffer+17)) = HTOLE16(asteroid_id);
					*((f32*)(buffer+19)) = HTOLE32F(asteroid->vel.x);
					*((f32*)(buffer+23)) = HTOLE32F(asteroid->vel.y);
					*((f32*)(buffer+27)) = HTOLE32F(asteroid->vel.z);
					*((f32*)(buffer+31)) = HTOLE32F(asteroid->start_pos.x);
					*((f32*)(buffer+35)) = HTOLE32F(asteroid->start_pos.y);
					*((f32*)(buffer+39)) = HTOLE32F(asteroid->start_pos.z);
					*((f32*)(buffer+43)) = HTOLE32F(asteroid->rot);
					*((f32*)(buffer+47)) = HTOLE32F(asteroid->scale);

					if (send_all) {
						for (uint16_t pid = 0; pid <= (uint16_t)states[all_clients[i].state_index].state->max_player_id; pid++) {
							if (states[all_clients[i].state_index].state->players[pid].valid)
								sendto(fd, buffer, 51, 0, (struct sockaddr*)&states[all_clients[i].state_index].state->players[pid].address, sizeof(struct sockaddr_in));
						}
					} else {
						sendto(fd, buffer, 51, 0, (struct sockaddr*)&source, source_len);
					}

					skip_asteroid_destroyed:
					mutex_unlock(&(states[all_clients[i].state_index].state->lock));
					break;
				}
			}

			mutex_unlock(&main_lock);
			break;
		case MSG_TYPE_EXO_HIT_RECVD:
			break;
		case MSG_TYPE_ASTEROID_DESTROYED_RECVD:
			break;
		case MSG_TYPE_PLAYER_DISCONNECTED_RECVD:
			break;
		case MSG_TYPE_REGISTER:
			if (msg_len < 19) {
				fputs("Packet to small for its type!\n", stderr);
				break;
			}

			uint64_t requested_epoch = LE64TOH(*((uint64_t*) &(buffer[9])));
			uint32_t num_asteroids = LE16TOH(*((uint16_t*) &(buffer[17])));
			if (num_asteroids == 0)
				num_asteroids = 0x10000;
			else if (num_asteroids < 0x10)
				goto register_error;

			if (current_time >= requested_epoch * 1000000) // TODO: cap the upper limit, not wait 5 years for a game?
				goto register_error;

			uint8_t found = 0;
			uint64_t client_index;
			uint64_t state_index;
			uint64_t available = 0;

			mutex_lock(&main_lock);

			for (uint64_t i = 0; i < num_clients; i++) {
				if (all_clients[i].address.sin_addr.s_addr == source.sin_addr.s_addr && all_clients[i].address.sin_port == source.sin_port && all_clients[i].valid) {
					if (all_clients[i].epoch != requested_epoch) { // client is already registered with a different epoch...
						goto register_error_unlock;
					} else {
						state_index = all_clients[i].state_index;
						goto send_id;
					}
				} else if (!all_clients[i].valid && available) {
					available = 1;
					client_index = i;
				}
			}

			if (available) {
				available = 0;
			} else {
				void *tmp = realloc(all_clients, sizeof(*all_clients) * (num_clients + 1));
				if (tmp == 0)
					goto register_error_unlock;

				all_clients = tmp;
				client_index = num_clients;
				num_clients++;
			}

			for (uint64_t i = 0; i < num_states; i++) {
				if (states[i].epoch == requested_epoch) {
					found = 1;
					state_index = i;
					break;
				} else if (states[i].epoch == 0 && !available) {
					available = 1;
					state_index = i;
				}
			}

			struct current_state *state;
			uint64_t player_index;
			if (found) {
				state = states[state_index].state;
				mutex_lock(&(state->lock));
				for (uint16_t i = 0; i <= (uint16_t) state->max_player_id; i++) {
					if (!state->players[i].valid) {
						available = 1;
						player_index = i;
						break;
					}
				}

				if (!available) {
					if (state->max_player_id == 0xFF) {
						goto register_error_unlock;
					}

					void *tmp = realloc(state->players, sizeof(*(state->players)) * (state->max_player_id + 2));
					if (tmp == 0) {
						goto register_error_unlock;
					}
					state->players = tmp;

					state->max_player_id++;
					player_index = state->max_player_id;
				}
			} else {
				player_index = 0;
				if (!available) {
					void *tmp = realloc(states, sizeof(*states) * (num_states + 1));
					if (tmp == 0)
						goto register_error_unlock;

					states = tmp;
					state_index = num_states;
					num_states++;
				}

				state = malloc(sizeof(*state));
				if (state == 0) // AAAAAAA OOM
					goto register_error_unlock;

				states[state_index].state = state;
				states[state_index].epoch = requested_epoch;

				memset(state, 0, sizeof(*state));

				state->players = malloc(sizeof(*(state->players)));
				if (state->players == 0) {
					goto register_error_clear;
				}
				state->max_player_id = 0;

				state->asteroids = malloc(sizeof(*(state->asteroids)) * num_asteroids);
				if (state->asteroids == 0) {
					free(state->players);

					goto register_error_clear;
				}

				state->max_asteroid_id = num_asteroids - 1;
				memset(state->asteroids, 0, sizeof(*(state->asteroids)) * num_asteroids);
				state->damage = 0;

				state->damage_index = malloc(sizeof(*state->damage_index) * exo_numvert);
				if (state->damage_index == 0) {
					free(state->players);
					free(state->asteroids);

					goto register_error_clear;
				}
				memset(state->damage_index, 0, sizeof(*state->damage_index) * exo_numvert);

				state->epoch = requested_epoch;
				state->state_index = state_index;

				mutex_lock(&(state->lock));

				state->world_stack = mmap(0, TOTAL_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (state->world_stack == 0) {
					goto register_error_clear_all;
				}
				mprotect(state->world_stack, BLOCKED_STACK_SIZE, PROT_NONE);

				state->asteroid_stack = mmap(0, TOTAL_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (state->world_stack == 0) {
					munmap(state->world_stack, TOTAL_STACK_SIZE);
					goto register_error_clear_all;
				}
				mprotect(state->asteroid_stack, BLOCKED_STACK_SIZE, PROT_NONE);

				state->player_stack = mmap(0, TOTAL_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (state->world_stack == 0) {
					munmap(state->world_stack, TOTAL_STACK_SIZE);
					munmap(state->asteroid_stack, TOTAL_STACK_SIZE);
					goto register_error_clear_all;
				}
				mprotect(state->player_stack, BLOCKED_STACK_SIZE, PROT_NONE);

				state->world_thread = clone(&main_loop_world, state->world_stack+TOTAL_STACK_SIZE, CLONE_FLAGS, state);
				state->asteroid_thread = clone(&main_loop_send_asteroids, state->asteroid_stack+TOTAL_STACK_SIZE, CLONE_FLAGS, state);
				state->player_thread = clone(&main_loop_send_players, state->player_stack+TOTAL_STACK_SIZE, CLONE_FLAGS, state);
			}

			state->players[player_index].address.sin_family = AF_INET;
			state->players[player_index].address.sin_addr.s_addr = source.sin_addr.s_addr;
			state->players[player_index].address.sin_port = source.sin_port;
			state->players[player_index].client_index = client_index;
			state->players[player_index].pos.x = 0.f;
			state->players[player_index].pos.y = 0.f;
			state->players[player_index].pos.z = 0.f;
			state->players[player_index].vel.x = 0.f;
			state->players[player_index].vel.y = 0.f;
			state->players[player_index].vel.z = 0.f;
			state->players[player_index].valid = 1;

			all_clients[client_index].state_index = state_index;
			all_clients[client_index].address.sin_addr.s_addr = source.sin_addr.s_addr;
			all_clients[client_index].address.sin_port = source.sin_port;
			all_clients[client_index].last_recvd = current_time;
			all_clients[client_index].player_id = player_index;
			all_clients[client_index].epoch = requested_epoch;
			all_clients[client_index].valid = 1;

			state->sun_pos = 0.f;

			mutex_unlock(&(state->lock));

			send_id:
			*((uint16_t*)(buffer+10)) = HTOLE16(states[state_index].max_asteroid_id + 1);

			mutex_unlock(&main_lock);
			buffer[8] = MSG_TYPE_REGISTER_ACCEPTED;
			buffer[9] = (uint8_t)player_index;
			sendto(fd, buffer, 10, 0, (struct sockaddr*)&source, source_len);

			break;

			register_error_clear_all:
			// not really all (stacks untouched), TODO: rename

			free(states[state_index].state->damage_index);
			free(states[state_index].state->players);
			free(states[state_index].state->asteroids);

			register_error_clear:
			free(states[state_index].state);
			states[state_index].epoch = 0;

			register_error_unlock:
			mutex_unlock(&main_lock);

			register_error:
			buffer[8] = MSG_TYPE_BAD_REGISTER_VALUE;
			sendto(fd, buffer, 9, 0, (struct sockaddr*)&source, source_len);
			break;

		default:
			fputs("Invalid message type!\n", stderr);
			break;
		}
	}

	return 0;
}
