// Host stand-in for Circle's <circle/writebuffer.h> (fuzzing shim): writes pass straight
// through to the wrapped serial device.
#ifndef _circle_writebuffer_h
#define _circle_writebuffer_h
#include <circle/serial.h>
class CWriteBufferDevice
{
public:
	CWriteBufferDevice (CSerialDevice *pTarget) : m_pTarget (pTarget) {}
	int Write (const void *pBuffer, size_t nCount) { return m_pTarget->Write (pBuffer, nCount); }
	void Update (void) {}
private:
	CSerialDevice *m_pTarget;
};
#endif
