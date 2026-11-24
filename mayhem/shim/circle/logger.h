// Host stand-in for Circle's <circle/logger.h>: log calls evaluate their arguments
// through a printf-format-checked sink and discard the text (fuzzing shim).
#ifndef _circle_logger_h
#define _circle_logger_h
#include <circle/types.h>
#include <assert.h>
static inline void shim_log_sink (const char *, ...) {}
#define LOGMODULE(name) static const char FromLogModule[] __attribute__ ((unused)) = name
#define LOGPANIC(...) shim_log_sink (__VA_ARGS__)
#define LOGERR(...)   shim_log_sink (__VA_ARGS__)
#define LOGWARN(...)  shim_log_sink (__VA_ARGS__)
#define LOGNOTE(...)  shim_log_sink (__VA_ARGS__)
#define LOGDBG(...)   shim_log_sink (__VA_ARGS__)
#endif
#ifndef _circle_logger_class
#define _circle_logger_class
enum TLogSeverity { LogPanic, LogError, LogWarning, LogNotice, LogDebug };
class CLogger
{
public:
	static CLogger *Get (void) { static CLogger s; return &s; }
	void Write (const char *, TLogSeverity, const char *, ...) {}
};
#endif
