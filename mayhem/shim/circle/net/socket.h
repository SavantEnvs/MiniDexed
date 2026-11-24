// Host stand-in for Circle's <circle/net/socket.h>. Datagrams come from, and replies go to,
// hooks the harness defines (fuzzing shim; no real network).
#ifndef _circle_net_socket_h
#define _circle_net_socket_h
#include <circle/types.h>
#include <circle/net/ipaddress.h>
#include <circle/net/netsubsystem.h>
#define FRAME_BUFFER_SIZE 1600
int ShimSocketReceiveFrom (u16 nOwnPort, void *pBuffer, unsigned nLength, CIPAddress *pForeignIP, u16 *pForeignPort);
int ShimSocketSendTo (u16 nOwnPort, const void *pBuffer, unsigned nLength, const CIPAddress &rForeignIP, u16 nForeignPort);
// TCP (FTP control/data connections): supplied by the harness that uses them.
class CSocket;
int ShimTcpReceive (CSocket *pSocket, void *pBuffer, unsigned nLength);
int ShimTcpSend (CSocket *pSocket, const void *pBuffer, unsigned nLength);
int ShimTcpConnect (CSocket *pSocket, const CIPAddress &rForeignIP, u16 nForeignPort);
CSocket *ShimTcpAccept (CSocket *pSocket);
class CSocket
{
public:
	CSocket (CNetSubSystem *, int nProtocol) : m_nProtocol (nProtocol), m_nOwnPort (0) {}
	int Bind (u16 nOwnPort) { m_nOwnPort = nOwnPort; return 0; }
	int ReceiveFrom (void *pBuffer, unsigned nLength, int, CIPAddress *pForeignIP, u16 *pForeignPort)
	{ return ShimSocketReceiveFrom (m_nOwnPort, pBuffer, nLength, pForeignIP, pForeignPort); }
	int SendTo (const void *pBuffer, unsigned nLength, int, CIPAddress &rForeignIP, u16 nForeignPort)
	{ return ShimSocketSendTo (m_nOwnPort, pBuffer, nLength, rForeignIP, nForeignPort); }
	int Connect (CIPAddress &rForeignIP, u16 nForeignPort) { return ShimTcpConnect (this, rForeignIP, nForeignPort); }
	int Listen (unsigned = 4) { return 0; }
	CSocket *Accept (CIPAddress *, u16 *) { return ShimTcpAccept (this); }
	int Receive (void *pBuffer, unsigned nLength, int) { return ShimTcpReceive (this, pBuffer, nLength); }
	int Send (const void *pBuffer, unsigned nLength, int) { return ShimTcpSend (this, pBuffer, nLength); }
	const u8 *GetForeignIP (void) const { static const u8 IP[4] = {10, 0, 0, 1}; return IP; }
	u16 GetOwnPort (void) const { return m_nOwnPort; }
private:
	int m_nProtocol;
	u16 m_nOwnPort;
};
#endif
