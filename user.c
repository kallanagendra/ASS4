#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

#include "oss.h"

//maximum values for:
#define R 5
#define S 1000
#define P 99

static int shmid = -1, mqid = -1;

struct arguments{
	int id;
} args = {0};

static struct virt_clock* attach_shm(){
	key_t key = ftok(SHARED_PATH, MEMORY_KEY);  //get a key for the shared memory
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	shmid = shmget(key, sizeof(struct virt_clock), 0);
	if(shmid == -1){
		perror("shmget");
		return NULL;
	}

	struct virt_clock *clock = (struct virt_clock *) shmat(shmid, NULL, 0);
	if(clock == NULL){
		perror("shmat");
		return NULL;
	}

	key = ftok(SHARED_PATH, MESSAGE_KEY);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	mqid = msgget(key, 0);
	if(mqid == -1){
		perror("msgget");
		return NULL;
	}

	return clock;
}

int main(const int argc, char * const argv[]){

	if(argc != 2){
		fprintf(stderr, "Error: Missing id argument\n");
		return 1;
	}

	args.id = atoi(argv[1]);

	//we don't use the shared memory, but we attach it
	struct virt_clock *clock = attach_shm();
	if(clock == NULL){
		return 1;
	}

	srand(getpid() * (args.id+1));	//init rand()

	const pid_t for_parent = getppid();
	const pid_t for_me		 = getpid();

	enum pstate state = READY;
  while(state != TERMINATED){

		//wait to be scheduled
		struct msgbuf msg;
		if(msgrcv(mqid, (void*)&msg, MSG_SIZE, for_me, 0) < 0){
			perror("msgrcv");
			break;
		}

		const int quantum = msg.value;
		const int action = rand() % 4;	//choose what to do

		if(action == 0){	//just terminate, without using whole quantum
			state = TERMINATED;
			msg.clock.seconds = 0;
			msg.clock.nanoseconds = 0;

		}else if(action == 1){	//terminate, using the whole allocated quantum
			state = TERMINATED;
			msg.clock.seconds = 0;
			msg.clock.nanoseconds = quantum;

		}else if(action == 2){	//waiting on some event
			state = WAITING;
			msg.clock.seconds = rand() % R;
			msg.clock.nanoseconds = rand() % S;

		}else if(action == 3){	//interrupted from scheduler
			state = READY;
			//use random amount of quantum, when preemted
			float preempt_prob = 1.0 + (rand() % P);
			msg.clock.seconds = 0;
			msg.clock.nanoseconds = (long)((float)quantum / (100.0 / preempt_prob));
		}

		//send new state to scheduler
		msg.mtype = for_parent;
		msg.value = state;
		if(msgsnd(mqid, (void*)&msg, MSG_SIZE, 0) < 0){
			perror("msgsnd");
			break;
		}
  }

  shmdt(clock);
  return EXIT_SUCCESS;
}
