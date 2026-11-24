// Host stand-in for Circle's <circle/sched/task.h>: no scheduler; the harness calls Run().
#ifndef _circle_sched_task_h
#define _circle_sched_task_h
#define TASK_STACK_SIZE 0x8000
class CTask
{
public:
	CTask (unsigned = TASK_STACK_SIZE, bool = false) {}
	virtual ~CTask (void) {}
	virtual void Run (void) {}
	void Start (void) {}
};
#endif
