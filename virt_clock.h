struct virt_clock{
	int seconds;
	int nanoseconds;

	int tick;	//how much nanoseconds we add to clock on a tick
};

//nanosecond in 1 second
#define NS_IN_SEC 1000000000

//check if clock is past a some time(mark)
int clock_compare(struct virt_clock *A, struct virt_clock * B);
void clock_tick(struct virt_clock *clock);
void clock_add(struct virt_clock *A, const struct virt_clock * B);
void clock_sub(const struct virt_clock *A, const struct virt_clock *B, struct virt_clock *C);

int clock_fork_check(struct virt_clock *A);	//advance clock and decide, shall we start a new process
int clock_passed(const struct virt_clock *mark, const struct virt_clock *clk);
