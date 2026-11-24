// Host stand-in for Circle's <circle/startup.h> (fuzzing shim). reboot () cannot return on the
// device; here it throws CShimReboot, which the harness catches to end the input.
#ifndef _circle_startup_h
#define _circle_startup_h
struct CShimReboot {};
[[noreturn]] inline void reboot (void) { throw CShimReboot (); }
[[noreturn]] inline void halt (void) { throw CShimReboot (); }
#endif
