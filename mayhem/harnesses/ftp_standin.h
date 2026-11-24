// Interface of mayhem/harnesses/ftp_standin.cc (FTP worker stand-ins), see there.
#ifndef _mayhem_ftp_standin_api_h
#define _mayhem_ftp_standin_api_h
#include <stddef.h>
#include <stdint.h>
#include <string>
void FtpSession (const uint8_t *pData, size_t nSize);	// one CFTPWorker::Run () session
extern std::string g_FtpControlOut, g_FtpDataOut;	// what the worker sent (capped at 64 KiB)
bool FtpFile (const char *pFatFsPath, std::string *pData);	// in-memory SD card after the session
#endif
