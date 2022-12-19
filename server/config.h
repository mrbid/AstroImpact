#define EXO_IMPACT_RADIUS 1.14f
#define EXO_TARGET_RADIUS 1.f // if you don't want asteroids scraping the edge, set this to somewhere less than EXO_IMPACT_RADIUS

#define MIN_PACKET_SIZE 11
#define MAX_PACKET_SIZE 512

#define PLAYER_SEND_UTIME 50000 // 20hz
#define ASTEROID_SEND_UTIME 1000000 // 1hz

#define USABLE_STACK_SIZE 128*4096	// 512K, there's not really much stack in usage with these threads
#define BLOCKED_STACK_SIZE 16*4096	// 64K, this is the area below the stack to map as PROT_NONE -
					// segfault if we run out rather than overflowing into whatever may have happened to be there originally

#define TOTAL_STACK_SIZE BLOCKED_STACK_SIZE + USABLE_STACK_SIZE

#define CLONE_FLAGS CLONE_FILES | CLONE_FS | CLONE_IO | CLONE_VM | SIGCHLD
