// Force-included (-include) when compiling src/net/ftpworker.cpp and the FTP harness. The real
// src/net/mdnspublisher.h pulls in Circle's scheduler mutex/event classes; the FTP worker only
// names CmDNSPublisher to unpublish services before a reboot (skipped when it is passed none,
// as the harness does), so this declares just that and defines the real header's include guard.
#ifndef _mayhem_ftp_standin_h
#define _mayhem_ftp_standin_h
#define _circle_net_mdnspublisher_h
#include <circle/types.h>
class CmDNSPublisher
{
public:
	static constexpr const char *ServiceTypeAppleMIDI = "_apple-midi._udp";
	static constexpr const char *ServiceTypeFTP = "_ftp._tcp";
	boolean UnpublishService (const char *pServiceName);
	boolean UnpublishService (const char *pServiceName, const char *pServiceType, u16 usServicePort);
};
#endif
