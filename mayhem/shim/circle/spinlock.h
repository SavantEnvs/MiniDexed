// Host stand-in for Circle's <circle/spinlock.h> (fuzzing shim; single-threaded host).
#ifndef _circle_spinlock_h
#define _circle_spinlock_h
#include <circle/types.h>
class CSpinLock
{
public:
	CSpinLock (unsigned = 0) {}
	void Acquire (void) {}
	void Release (void) {}
};
#endif
