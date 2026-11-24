// libFuzzer harness: FTP control channel of the built-in FTP server (src/net/ftpworker.cpp).
//
// Drives the real CFTPWorker::Run () session loop: login (USER/PASS), PORT address parsing,
// PASV, TYPE, CWD/CDUP/PWD path resolution, LIST/NLST, RETR/STOR, DELE, MKD, RNFR/RNTO, QUIT.
// The fuzz input is the text the client sends on the control connection. It is split at '\n'
// and each line (a trailing '\r' dropped) is returned by one control-socket Receive () with a
// CRLF terminator, the way a client's command arrives in one TCP segment, capped at the receive
// buffer size. (Upstream strips that terminator by writing m_CommandBuffer[nReceiveBytes - 2],
// so a segment shorter than 2 bytes writes before the buffer; that bug is reported in the PR,
// and the framing keeps every input from stopping on it.) Once the
// input is used up Receive () returns -1 (connection closed) and Run () returns on its own.
// QUIT ends in reboot (), which the shim makes throw CShimReboot (caught here).
// Data connections (PORT/PASV) always connect; a data socket delivers one fixed chunk to STOR
// and then reports end of data; data sent to the client is read and discarded.
// The SD card is an in-memory FatFs stand-in with a small fixed tree, reset per input (all in
// mayhem/harnesses/ftp_standin.cc).
// Per-input state: the worker (constructed per input; its instance counter returns to 0 in the
// destructor), the file tree, and the virtual clock are all reset per input.
#include <stdint.h>
#include <stddef.h>
#include "ftp_standin.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *pData, size_t nSize)
{
	FtpSession (pData, nSize);
	return 0;
}
