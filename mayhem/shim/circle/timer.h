// Host stand-in for Circle's <circle/timer.h>: a virtual clock the harness advances.
#ifndef _circle_timer_h
#define _circle_timer_h
#include <circle/types.h>
#ifndef HZ
#define HZ 100			// ticks per second, as Circle's timer.h
#endif
typedef uintptr TKernelTimerHandle;
typedef void TKernelTimerHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext);
extern u64 g_ShimClockTicks;	// microseconds, owned by the harness
class CTimer
{
public:
	static u64 GetClockTicks (void) { return g_ShimClockTicks; }
	static CTimer *Get (void) { static CTimer s; return &s; }
	unsigned GetTicks (void) { return (unsigned) (g_ShimClockTicks / 10000); }
};
#endif
