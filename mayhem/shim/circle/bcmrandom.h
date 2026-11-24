// Host stand-in for Circle's <circle/bcmrandom.h>: deterministic generator (fuzzing shim).
#ifndef _circle_bcmrandom_h
#define _circle_bcmrandom_h
#include <circle/types.h>
class CBcmRandomNumberGenerator
{
public:
	u32 GetNumber (void) { return m_nState = m_nState * 1103515245u + 12345u; }
	u32 m_nState = 1;
};
#endif
