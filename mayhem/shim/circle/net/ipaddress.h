// Host stand-in for Circle's <circle/net/ipaddress.h> (fuzzing shim).
#ifndef _circle_net_ipaddress_h
#define _circle_net_ipaddress_h
#include <circle/types.h>
#include <circle/string.h>
#include <stdio.h>
#include <string.h>
#define IP_ADDRESS_SIZE 4
class CIPAddress
{
public:
	CIPAddress (void) : m_nAddress (0) {}
	CIPAddress (u32 nAddress) : m_nAddress (nAddress) {}
	void Set (u32 nAddress) { m_nAddress = nAddress; }
	void Set (const CIPAddress &rAddress) { m_nAddress = rAddress.m_nAddress; }
	void Set (const u8 *pAddress) { memcpy (&m_nAddress, pAddress, IP_ADDRESS_SIZE); }
	void CopyTo (u8 *pBuffer) const { memcpy (pBuffer, &m_nAddress, IP_ADDRESS_SIZE); }
	bool operator== (const CIPAddress &r) const { return m_nAddress == r.m_nAddress; }
	bool operator!= (const CIPAddress &r) const { return m_nAddress != r.m_nAddress; }
	operator u32 (void) const { return m_nAddress; }
	void Format (CString *pString) const
	{
		char b[20]; const u8 *p = (const u8 *) &m_nAddress;
		snprintf (b, sizeof b, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
		*pString = b;
	}
private:
	u32 m_nAddress;
};
#endif
