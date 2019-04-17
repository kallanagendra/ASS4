#include "virt_clock.h"

//quantum in nanoseconds
#define QUANTUM 1000000

#define SHARED_PATH "/home"
#define MESSAGE_KEY 1234
#define MEMORY_KEY 5678

struct msgbuf {
	long mtype;
	int value;
	struct virt_clock clock;
};
#define MSG_SIZE sizeof(int) + sizeof(struct virt_clock)

enum pstate	{ DEAD=0, READY, WAITING, TERMINATED, PROC_STATES};
