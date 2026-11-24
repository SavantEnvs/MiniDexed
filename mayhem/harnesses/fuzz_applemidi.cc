// libFuzzer harness: AppleMIDI / RTP-MIDI network session (src/net/applemidi.cpp).
//
// Drives the real CAppleMIDIParticipant::Run() loop: each loop iteration both UDP sockets
// (control port 5004, MIDI port 5005) are polled, exactly as on the device. The fuzz input is a
// sequence of datagram records, one delivered per iteration:
//   [flags:1][advance:1][len:2 LE][payload:len]
//     flags bit0  0 = control socket, 1 = MIDI socket
//     flags bit1  datagram comes from the alternate peer IP (10.0.0.2 instead of 10.0.0.1)
//     flags bit2  datagram comes from the alternate peer port (+100)
//     advance     virtual clock advance before the datagram, in 10 ms units (reaches the
//                 invitation/sync timeouts and receiver-feedback paths)
// Payload length is capped at the socket buffer size, as a real UDP receive would truncate.
// When the input is used up, the shim scheduler's Yield() throws CShimInputDone, which ends the
// endless task loop (the only way out of Run(); the participant object is discarded per input).
// Per-input state: the participant, sockets and clock are recreated/reset for every input.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include "net/applemidi.h"

u64 g_ShimClockTicks = 1000000;

namespace {

const u8 *g_pData;
size_t g_nSize, g_nPos;
// current record
bool g_bHave, g_bMIDI, g_bAltIP, g_bAltPort;
const u8 *g_pPayload;
size_t g_nPayload;
size_t g_nMIDIBytesReceived;
volatile u8 g_Sink;

bool NextRecord (void)
{
	g_bHave = false;
	if (g_nSize - g_nPos < 4)
		return false;
	const u8 *p = g_pData + g_nPos;
	g_bMIDI = p[0] & 1;
	g_bAltIP = p[0] & 2;
	g_bAltPort = p[0] & 4;
	g_ShimClockTicks += (u64) p[1] * 10000;
	size_t nLen = p[2] | (p[3] << 8);
	g_nPos += 4;
	if (nLen > g_nSize - g_nPos)
		nLen = g_nSize - g_nPos;
	g_pPayload = g_pData + g_nPos;
	g_nPayload = nLen;
	g_nPos += nLen;
	g_bHave = true;
	return true;
}

class CHandler : public CAppleMIDIHandler
{
public:
	void OnAppleMIDIDataReceived (const u8 *pData, size_t nSize) override
	{
		// Touch every byte the parser hands on, as the MIDI device consumer would.
		for (size_t i = 0; i < nSize; i++)
			g_Sink ^= pData[i];
		g_nMIDIBytesReceived += nSize;
	}
	void OnAppleMIDIConnect (const CIPAddress *pIPAddress, const char *pName) override
	{
		g_Sink ^= (u8) strlen (pName) ^ (u8) (u32) *pIPAddress;
	}
	void OnAppleMIDIDisconnect (const CIPAddress *pIPAddress, const char *pName) override
	{
		g_Sink ^= (u8) strlen (pName) ^ (u8) (u32) *pIPAddress;
	}
};

} // namespace

int ShimSocketReceiveFrom (u16 nOwnPort, void *pBuffer, unsigned nLength, CIPAddress *pForeignIP, u16 *pForeignPort)
{
	const bool bMIDISocket = nOwnPort == 5005;
	if (!g_bHave || g_bMIDI != bMIDISocket)
		return 0;
	size_t n = g_nPayload < nLength ? g_nPayload : nLength;
	memcpy (pBuffer, g_pPayload, n);
	pForeignIP->Set (g_bAltIP ? 0x0200000Au : 0x0100000Au);
	*pForeignPort = (u16) (nOwnPort + (g_bAltPort ? 100 : 0));
	return (int) n;
}

int ShimSocketSendTo (u16, const void *pBuffer, unsigned nLength, const CIPAddress &, u16)
{
	const u8 *p = (const u8 *) pBuffer;
	for (unsigned i = 0; i < nLength; i++)
		g_Sink ^= p[i];
	return (int) nLength;
}

void ShimSchedulerYield (void)
{
	if (!NextRecord ())
		throw CShimInputDone ();
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *pData, size_t nSize)
{
	g_pData = pData;
	g_nSize = nSize;
	g_nPos = 0;
	g_nMIDIBytesReceived = 0;
	g_ShimClockTicks = 1000000;
	NextRecord ();

	CBcmRandomNumberGenerator Random;
	CHandler Handler;
	CAppleMIDIParticipant *pParticipant = new CAppleMIDIParticipant (&Random, &Handler, "MiniDexed");
	if (pParticipant->Initialize ())
	{
		try
		{
			pParticipant->Run ();
		}
		catch (const CShimInputDone &)
		{
		}
	}
	delete pParticipant;
	return 0;
}
