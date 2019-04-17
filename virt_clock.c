#include <stdlib.h>
#include "virt_clock.h"

//maxTimeBetweenNewProcs
#define B_SEC 2
#define B_NS  100

//Like strcmp, but for clocks
int clock_compare(struct virt_clock *A, struct virt_clock *B){

  if (A->seconds > B->seconds) {
      return 1;	//after
  }else if (A->seconds < B->seconds) {
      return -1;	//before
	}else if (A->seconds == B->seconds){

    if(A->nanoseconds > B->nanoseconds)
		  return 1;	//past end
    else if(A->nanoseconds < B->nanoseconds)
      return -1;  //before
    else
      return 0; //equal
  } else {
      return 0;	//equal
  }
}


void clock_add(struct virt_clock *A, const struct virt_clock * B){
  A->seconds  += B->seconds;
	A->nanoseconds += B->nanoseconds;
	if(A->nanoseconds > NS_IN_SEC){
		A->seconds++;
		A->nanoseconds -= NS_IN_SEC;
	}
}

void clock_sub(const struct virt_clock *A, const struct virt_clock *B, struct virt_clock *C){
    if ((B->nanoseconds - A->nanoseconds) < 0) {
        C->seconds  = B->seconds  - A->seconds - 1;
        C->nanoseconds = A->nanoseconds - B->nanoseconds;
    } else {
        C->seconds  = B->seconds  - A->seconds;
        C->nanoseconds = B->nanoseconds - A->nanoseconds;
    }
}

//checks if time mark passed current clock
int clock_passed(const struct virt_clock *mark, const struct virt_clock *clk){
  if( (clk->seconds >= mark->seconds) ||
      ( (clk->seconds  == mark->seconds) &&
        (clk->nanoseconds >= mark->nanoseconds))  ){
    return 1;
  }else{
    return 0;
  }
}

int clock_fork_check(struct virt_clock *oss_clock){

  static const  struct virt_clock tick    = {.seconds = 1, .nanoseconds = 1000};
  static        struct virt_clock fork_at = {.seconds = 0, .nanoseconds = 0};

  clock_add(oss_clock, &tick);  //advance clock

  //check if we need to fork
  if(clock_passed(&fork_at, oss_clock)){

    //generate next fork time
    fork_at.seconds = rand() % B_SEC;
    fork_at.nanoseconds = rand() % B_NS;
    clock_add(&fork_at, oss_clock);

    return 1;
  }

  return 0;
}
