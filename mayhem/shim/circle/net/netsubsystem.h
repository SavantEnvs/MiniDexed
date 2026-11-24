// Host stand-in for Circle's <circle/net/netsubsystem.h> (fuzzing shim).
#ifndef _circle_net_netsubsystem_h
#define _circle_net_netsubsystem_h
#include <circle/net/ipaddress.h>
class CNetConfig
{
public:
	const CIPAddress *GetIPAddress (void) const { return &m_IPAddress; }
private:
	CIPAddress m_IPAddress {0x0A00000A};	// 10.0.0.10
};
class CNetSubSystem
{
public:
	static CNetSubSystem *Get (void) { static CNetSubSystem s; return &s; }
	CNetConfig *GetConfig (void) { return &m_Config; }
private:
	CNetConfig m_Config;
};
#endif
