#pragma once

extern int fd;
extern uint32_t main_lock;
extern struct state_holder *states;
extern uint64_t num_states;
extern struct client_holder *clients;
extern uint64_t num_clients;
