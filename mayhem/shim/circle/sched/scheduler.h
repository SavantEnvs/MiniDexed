// Host stand-in for Circle's <circle/sched/scheduler.h>. On the device Yield() hands the CPU to
// other tasks; here it returns to the harness: ShimSchedulerYield() throws CShimInputDone once
// the current input is fully delivered, which ends the task's endless Run() loop.
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h
struct CShimInputDone {};
void ShimSchedulerYield (void);
class CScheduler
{
public:
	static CScheduler *Get (void) { static CScheduler s; return &s; }
	void Yield (void) { ShimSchedulerYield (); }
};
#endif
