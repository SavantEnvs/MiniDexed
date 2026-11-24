// Host stand-in for Circle's <circle/serial.h> (fuzzing shim). Read() hands out the bytes the
// harness queued in g_ShimSerialRx, at most nCount per call, like a UART receive buffer would;
// Write() appends the transmitted bytes to g_ShimSerialTx so a harness or test can inspect
// MIDI Thru output. (The device object is a private member of its owner, hence the globals.)
#ifndef _circle_serial_h
#define _circle_serial_h
#include <circle/types.h>
#include <string.h>
#include <vector>
#define SERIAL_OPTION_ONLCR	(1 << 0)
struct TShimSerialRx { const u8 *pData = 0; size_t nSize = 0, nPos = 0; };
inline TShimSerialRx g_ShimSerialRx;
inline std::vector<u8> g_ShimSerialTx;
class CInterruptSystem;
class CSerialDevice
{
public:
	CSerialDevice (CInterruptSystem * = 0, boolean = FALSE, unsigned = 0) {}
	boolean Initialize (unsigned = 115200) { return TRUE; }
	unsigned GetOptions (void) const { return m_nOptions; }
	void SetOptions (unsigned nOptions) { m_nOptions = nOptions; }
	int Read (void *pBuffer, size_t nCount)
	{
		TShimSerialRx &r = g_ShimSerialRx;
		size_t n = r.nSize - r.nPos < nCount ? r.nSize - r.nPos : nCount;
		if (n)
			memcpy (pBuffer, r.pData + r.nPos, n);
		r.nPos += n;
		return (int) n;
	}
	int Write (const void *pBuffer, size_t nCount)
	{
		const u8 *p = (const u8 *) pBuffer;
		g_ShimSerialTx.insert (g_ShimSerialTx.end (), p, p + nCount);
		return (int) nCount;
	}
private:
	unsigned m_nOptions = 0;
};
#endif
