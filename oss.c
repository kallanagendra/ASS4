#include <stdarg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>


#include "oss.h"
#include "bit_vector.h"

//maximum started/running processes
#define MAX_STARTED 100
#define MAX_RUNNING 20

//maximum runtime in realtime seconds
#define MAX_RUNTIME 3

//we generate 70% normal, and 30% realtime processes
#define NORMAL_PROCS 70

enum pprio	{	NORMAL=0, REALTIME, NUM_PRIORITIES};
enum ptimes {	CPU=0, SYSTEM, LAST_BURST, START, WAIT, PROC_TIMES};
enum qprio  { HIGHEST=0, HIGH, MEDIUM, LOW, BLOCKED, NUM_QUEUES};
enum ave_timing { AVE_IDLE, AVE_TURN, AVE_WAIT, AVE_TIMES};

struct proc_info{	//process information
	int pid;
	int id;
  int priority;
	int state;
	struct virt_clock times[PROC_TIMES];
} procs[MAX_RUNNING];

struct time_stats{
  unsigned int num_started;
  unsigned int num_terminated;
  unsigned int num_lines;
  unsigned int num_blocked;
} stats = {0,0,0,0};

struct array_queue{
	int ids[MAX_RUNNING];	//holds process id from oss_shm->pcb[].id
	int len;
};

static int shmid = -1, mqid = -1; //memory and message queue identifiers
static struct virt_clock * oss_clock = NULL;  //shared memory region

static struct virt_clock timing[AVE_TIMES];

//quantum for each priority
static const unsigned int quants[NUM_QUEUES] = {QUANTUM, QUANTUM/2, QUANTUM/4, QUANTUM/8, 0};
//scheduling queues
static struct array_queue qs[NUM_QUEUES];


static void show_timing(void){

  timing[AVE_TURN].seconds /= (stats.num_started == 0) ? 1 : stats.num_started;
  timing[AVE_TURN].nanoseconds /= (stats.num_started == 0) ? 1 : stats.num_started;
  timing[AVE_WAIT].seconds /= (stats.num_blocked == 0) ? 1 : stats.num_blocked;
  timing[AVE_WAIT].nanoseconds /= (stats.num_blocked == 0) ? 1 : stats.num_blocked;

  puts("Timers:");
  printf("Average Turnaround: %i:%i\n", timing[AVE_TURN].seconds, timing[AVE_TURN].nanoseconds);
  printf("Average Wait: %i:%i\n", timing[AVE_WAIT].seconds, timing[AVE_WAIT].nanoseconds);
	printf("Total Idle: %i:%i\n", timing[AVE_IDLE].seconds, timing[AVE_IDLE].nanoseconds);
  puts("Processes:");
  printf("Started: %u\n", stats.num_started);
  printf("Blocked: %u\n", stats.num_blocked);
  printf("Terminated: %u\n", stats.num_terminated);
}

void log_line(const char *fmt, ...){

  if(++stats.num_lines > 10000){  //if max lines reached
    stdout = freopen("/dev/null", "w", stdout); //discard lines to null device
  }

  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static void clean_exit(const int rc){
  int i;

  show_timing();

  for(i=0; i < MAX_RUNNING; i++)
    if(procs[i].pid > 0)
      kill(procs[i].pid, SIGTERM);


  shmdt(oss_clock);
  shmctl(shmid, IPC_RMID, NULL);
  msgctl(mqid, IPC_RMID, NULL);
  exit(rc);
}

static struct virt_clock* attach_shm(){
	const key_t key = ftok(SHARED_PATH, MEMORY_KEY);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	shmid = shmget(key, sizeof(struct virt_clock), IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(shmid == -1){
		perror("shmget");
		return NULL;
	}

	struct virt_clock* addr = (struct virt_clock*) shmat(shmid, NULL, 0);
  if(addr == (void*)-1){
    perror("shmat");
    return NULL;
  }

  key_t msg_key = ftok(SHARED_PATH, MESSAGE_KEY);
	if(msg_key == -1){
		perror("ftok");
		return NULL;
	}

	mqid = msgget(msg_key, IPC_CREAT | IPC_EXCL | 0666);
	if(mqid == -1){
		perror("msgget");
		return NULL;
	}

  return addr;
}

static void signal_handler(const int sig){
  switch(sig){
    case SIGTERM:
    case SIGALRM:
      printf("OSS: Caught signal at %i:%i\n", oss_clock->seconds, oss_clock->nanoseconds);
      clean_exit(0);
      break;
    default:
      break;
  }
}

static void q_add(struct array_queue * q, const int pi){
  q->ids[q->len++] = pi;  //save procs index to queue
}

static int q_shift(struct array_queue *pq, const int pos){
  int i;
  unsigned int temp = pq->ids[pos];  //save procs index

  for(i=pos; i < pq->len-1; i++)
    pq->ids[i] = pq->ids[i+1];  //shift indexes left

  return temp;
}

static int start_user(){
  static unsigned int PID = 1;
  char id_buf[10];

  if(stats.num_started >= MAX_STARTED)
      return EXIT_SUCCESS;


  const int pi = search_bitvector(MAX_RUNNING); //find free bit
  if(pi == -1)  //no process info available
    return EXIT_SUCCESS;

  snprintf(id_buf, 10, "%i", pi);

  const int normal_chance = (rand() % 100);

  procs[pi].id = PID++;
  procs[pi].priority = (normal_chance <= NORMAL_PROCS) ? NORMAL : REALTIME;
  procs[pi].times[START] = *oss_clock;
	procs[pi].state = READY;	 //change our status to READY

  procs[pi].pid = fork();
  switch(procs[pi].pid){

    case -1:
      perror("fork");
      return EXIT_FAILURE;
      break;

    case 0: //child process
      execl("./user", "./user", id_buf, NULL);
      perror("execl");
      exit(EXIT_FAILURE);
      break;

    default:
      stats.num_started++;

      const int qi = (procs[pi].priority == REALTIME) ? HIGHEST : HIGH;
      q_add(&qs[qi], pi);

      log_line("OSS: Generating process with PID %u and putting it in queue %d at time %i:%i\n",  procs[pi].pid, qi, oss_clock->seconds, oss_clock->nanoseconds);

      break;
  }

  return EXIT_SUCCESS;
}


static void proc_cleanup(){
  int i;

  for(i=0; i < MAX_RUNNING; i++){
    if( (procs[i].pid > 0) &&
        (procs[i].state == TERMINATED)){

        stats.num_terminated++;

        clock_add(&timing[AVE_TURN], &procs[i].times[SYSTEM]);

        //wait = total_system - total cpu
        struct virt_clock res;
        clock_sub(&procs[i].times[CPU], &procs[i].times[SYSTEM], &res);
        clock_add(&timing[AVE_WAIT], &res);

        bzero(&procs[i], sizeof(struct proc_info));
        procs[i].state = DEAD;

        unset_bit(i);
    }
  }
}

//look in queues for a process that is READY
static struct proc_info * find_ready(int * qi, int *pi){
  int q,i;

  //for each queue, starting from highest priority one
  for(q=0; q < NUM_QUEUES; q++){ //start from realtime queue 0
    for(i=0; i < qs[q].len; i++){

      const int pidx = qs[q].ids[i];
      if(procs[pidx].state == READY){
        *qi = q;
        *pi = i;
        return &procs[pidx];
      }
    }
  }
  return NULL;
}

static enum pstate proc_update_times(const struct msgbuf * msg, struct proc_info * proc, const int q){
  enum pstate saved_state = proc->state;

  switch(msg->value){  //value holds new state of process
    case READY:
      proc->state = READY;
      proc->times[LAST_BURST].seconds  = 0;
      proc->times[LAST_BURST].nanoseconds = msg->clock.nanoseconds;
      clock_add(&proc->times[CPU], &proc->times[LAST_BURST]);


      log_line("OSS: Receiving that process with PID %u ran for %i nanoseconds\n", proc->id, proc->times[LAST_BURST].nanoseconds);
      if(msg->clock.nanoseconds != quants[q]){
        log_line("OSS: not using its entire quantum\n");
      }

      clock_add(oss_clock, &proc->times[LAST_BURST]);  //advance clock with burst time
      break;

    case WAITING:
      proc->state = WAITING;
      proc->times[WAIT].seconds	= msg->clock.seconds;
      proc->times[WAIT].nanoseconds	= msg->clock.nanoseconds;
      clock_add(&proc->times[WAIT], oss_clock);	//add clock to wait time


      stats.num_blocked++;
      log_line("OSS: Receiving that process with PID %u is WAITING for event(%i:%i) %i:%i\n",
          proc->id, proc->times[WAIT].seconds, proc->times[WAIT].nanoseconds, oss_clock->seconds, oss_clock->nanoseconds);
      break;

    case TERMINATED:
      proc->state = TERMINATED;
      proc->times[LAST_BURST].seconds  = 0;
      proc->times[LAST_BURST].nanoseconds = msg->clock.nanoseconds;
      clock_add(&proc->times[CPU], &proc->times[LAST_BURST]); //add burst to total cpu time

      log_line("OSS: Receiving that process with PID %u is terminating\n", proc->id);

      //system time = current time - time started
      clock_sub(&proc->times[START], oss_clock, &proc->times[SYSTEM]);
      break;

    default:
      printf("OSS: Invalid response from process\n");
      break;
  }

  return saved_state;
}

static void proc_update_q(enum pstate old_state, struct proc_info * proc, const int q, const int pos){
  if(proc->state == TERMINATED){
    q_shift(&qs[q], pos);
    qs[q].len--;
    printf("OSS: Removing terminated process with PID %u from queue %d\n", proc->id, q);

  }else if(proc->state == WAITING){

    log_line("OSS: Putting process with PID %u into queue %d\n", proc->id, BLOCKED);

    qs[BLOCKED].ids[qs[BLOCKED].len] = q_shift(&qs[q], pos);  //put process at end
    qs[q].len--;  //drop from q
    qs[BLOCKED].len++;  //enqueue at blocked

  }else if(proc->priority == REALTIME){
    log_line("OSS: Putting process with PID %u into queue %d\n", proc->id, HIGHEST);
    qs[q].ids[qs[q].len-1] = q_shift(&qs[q], pos);  //put process at end

  }else{
    //woken up process go to higest queue, rest, one level up
    int nextq = ((old_state == WAITING) && (proc->state == READY)) ? 1 : q + 1;
    if(nextq >= LOW){
      nextq = LOW;
    }

    log_line("OSS: Putting process with PID %u into queue %d\n", proc->id, nextq);

    if(nextq == q){ //if its the same q, don't change size
      qs[nextq].ids[qs[nextq].len-1] = q_shift(&qs[q], pos);
    }else{
      qs[nextq].ids[qs[nextq].len++] = q_shift(&qs[q], pos);
      qs[q].len--;  //drop process from old queue
    }
  }
}

// find a READY process from highest priority queue
static int proc_dispatch(){
  static int only_once = 0;
  static struct virt_clock idle_from = {.seconds=0, .nanoseconds=0};

  int q,pos;
  struct msgbuf msg;
  struct virt_clock tdispatch;

  bzero(&msg, sizeof(struct msgbuf));

  //print_qs();

  struct proc_info * proc = find_ready(&q, &pos);
  if(proc == NULL){

    if(only_once == 0)
      log_line("OSS: No ready process for proc_dispatch %i:%i\n", oss_clock->seconds, oss_clock->nanoseconds);


		idle_from = *oss_clock;
    only_once = 1; //don't print twice the 'no ready' message
    return EXIT_SUCCESS;

  }else{
    only_once = 0;

    //calculate idle time
    struct virt_clock ave_idle;
  	clock_sub(&idle_from, oss_clock, &ave_idle);
  	clock_add(&timing[AVE_IDLE], &ave_idle);

  	idle_from.seconds  = 0;
  	idle_from.nanoseconds = 0;
  }

  log_line("OSS: Dispatching process with PID %u from queue %i at time %i:%i\n", proc->id, q, oss_clock->seconds, oss_clock->nanoseconds);

  //calculate proc_dispatch time
  tdispatch.seconds = 0;
  tdispatch.nanoseconds = rand() % 2000;
  clock_add(oss_clock, &tdispatch);  //advance clock with proc_dispatch time


  log_line("OSS: total time this proc_dispatch was %i nanoseconds\n", tdispatch.nanoseconds);

  // send message to process
  msg.value = quants[q];  //set quant, based on process prio
  msg.mtype = proc->pid;  //send to process with PID

	if( (msgsnd(mqid, (void*)&msg, MSG_SIZE,      0) == -1) ||
      (msgrcv(mqid, (void*)&msg, MSG_SIZE, getpid(), 0) == -1) ){
		perror("msgsnd");
		return EXIT_FAILURE;
	}

  const enum pstate old_state = proc_update_times(&msg, proc, q);

  proc_update_q(old_state, proc, q, pos);

  return EXIT_SUCCESS;
}

//wake up WAITING procs
static void proc_unblock(struct array_queue * qblocked){
  int i;
  for(i=0; i < qblocked->len; i++){
    const int pi = qblocked->ids[i];

    if(	clock_passed(&procs[pi].times[WAIT], oss_clock)){
      //process is unblocked. change to ready and remove wait timer mark
      procs[pi].state = READY;
      procs[pi].times[WAIT].seconds = 0;
      procs[pi].times[WAIT].nanoseconds = 0;

      q_shift(qblocked, i); //remove from old queue
      qblocked->len--;
      const int qi = (procs[pi].priority == REALTIME) ? HIGHEST : HIGH;
      q_add(&qs[qi], pi);  // move to READY queue

      log_line("OSS: Unblocked PID %d at %i:%i\n", procs[pi].id, oss_clock->seconds, oss_clock->nanoseconds);
    }
  }
}

static void run_simulator(){

  while(stats.num_terminated < MAX_STARTED){ //loop until all procs are done
    if(clock_fork_check(oss_clock) == 1){ //advance the oss  clock
      start_user();  //make a new process
    }
    proc_unblock(&qs[BLOCKED]);
    if(proc_dispatch() == EXIT_FAILURE)
      break;
    proc_cleanup();
  }
  log_line("OSS: Total simulated time %i:%i\n", oss_clock->seconds, oss_clock->nanoseconds);
}

void init_simulator(){
  srand(getpid());

  oss_clock->seconds = oss_clock->nanoseconds = 0;

  bzero(&qs,   sizeof(struct array_queue)*NUM_QUEUES);
  bzero(timing, sizeof(struct virt_clock)*AVE_TIMES);
}

int main(void){

  signal(SIGTERM, signal_handler);
  signal(SIGALRM, signal_handler);
	signal(SIGCHLD, SIG_IGN);

  //alarm(MAX_RUNTIME);
  stdout = freopen("log.txt", "w", stdout);

  oss_clock = attach_shm();
  if(oss_clock == NULL)
    return EXIT_FAILURE;

  init_simulator();
  run_simulator();

  clean_exit(EXIT_SUCCESS);
}
