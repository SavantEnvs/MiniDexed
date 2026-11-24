// In-memory stand-ins for the FTP worker's collaborators (control/data TCP sockets, the SD
// card's FatFs, the scheduler/clock, CmDNSPublisher), shared by fuzz_ftp and the oracle.
// FtpSession () runs one real CFTPWorker::Run () session over the given control-channel bytes.
//
// Session model (src/net/ftpworker.cpp):
//
// Drives the real CFTPWorker::Run () session loop: login (USER/PASS), PORT address parsing,
// PASV, TYPE, CWD/CDUP/PWD path resolution, LIST/NLST, RETR/STOR, DELE, MKD, RNFR/RNTO, QUIT.
// The fuzz input is the text the client sends on the control connection. It is split at '\n'
// and each line (a trailing '\r' dropped) is returned by one control-socket Receive () with a
// CRLF terminator, the way a client's command arrives in one TCP segment, capped at the receive
// buffer size. Once the
// input is used up Receive () returns -1 (connection closed) and Run () returns on its own.
// QUIT ends in reboot (), which the shim makes throw CShimReboot (caught here).
// Data connections (PORT/PASV) always connect; a data socket delivers one fixed chunk to STOR
// and then reports end of data; data sent to the client is read and discarded.
// The SD card is an in-memory FatFs stand-in with a small fixed tree (below), reset per input.
// Per-input state: the worker (constructed per input; its instance counter returns to 0 in the
// destructor), the file tree, and the virtual clock are all reset per input.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include <circle/startup.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include <fatfs/ff.h>
#include <circle/net/in.h>
#include "net/ftpworker.h"
#include "ftp_standin.h"

u64 g_ShimClockTicks;

namespace {

const uint8_t *g_pData;
size_t g_nSize, g_nPos;
CSocket *g_pControl;
std::map<CSocket *, bool> g_DataSent;	// data socket -> its one chunk was delivered
volatile size_t g_Sink;

struct TEntry { bool bDir; std::string Data; };
std::map<std::string, TEntry> g_Files;	// key: FatFs path without trailing '/' ("SD:", "SD:/x")

void ResetFiles (void)
{
	g_Files.clear ();
	for (const char *pVol : {"SD:", "USB:"})
		g_Files[pVol] = {true, ""};
	g_Files["SD:/performance"] = {true, ""};
	g_Files["SD:/sysex"] = {true, ""};
	g_Files["SD:/sysex/voice"] = {true, ""};
	g_Files["SD:/minidexed.ini"] = {false, "ToneGenerators=8\r\n"};
	g_Files["SD:/performance.ini"] = {false, "MIDIChannel1=1\r\n"};
	g_Files["SD:/performance/000001_First.ini"] = {false, "Volume1=100\r\n"};
	g_Files["SD:/sysex/voice/000000_rom1a.syx"] = {false, std::string (4104, '\0')};
}

// Canonical form of a FatFs path, "VOL:" or "VOL:/a/b": FatFs accepts "VOL:a" for "VOL:/a",
// repeated and trailing slashes, and (FF_FS_RPATH = 2) "." and ".." segments.
std::string Key (const char *pPath)
{
	std::string s = pPath ? pPath : "";
	size_t nColon = s.find (':');
	std::string Vol = nColon == std::string::npos ? "SD:" : s.substr (0, nColon + 1);
	std::string Rest = nColon == std::string::npos ? s : s.substr (nColon + 1);
	std::vector<std::string> Parts;
	size_t i = 0;
	while (i <= Rest.size ())
	{
		size_t j = Rest.find ('/', i);
		if (j == std::string::npos)
			j = Rest.size ();
		std::string Seg = Rest.substr (i, j - i);
		if (Seg == "..")
		{
			if (!Parts.empty ())
				Parts.pop_back ();
		}
		else if (!Seg.empty () && Seg != ".")
			Parts.push_back (Seg);
		i = j + 1;
	}
	for (const std::string &Seg : Parts)
		Vol += "/" + Seg;
	return Vol;
}

std::string Parent (const std::string &s)	// "" for a volume root
{
	size_t n = s.rfind ('/');
	return n == std::string::npos ? std::string () : s.substr (0, n);
}

// FatFs directory iteration: one listing at a time (the worker never nests them).
std::vector<std::string> g_Listing;
size_t g_nListPos;
std::string g_OpenFile;
size_t g_nFilePos;

}  // namespace

std::string g_FtpControlOut, g_FtpDataOut;

bool FtpFile (const char *pPath, std::string *pData)
{
	auto it = g_Files.find (Key (pPath));
	if (it == g_Files.end ())
		return false;
	if (pData)
		*pData = it->second.Data;
	return true;
}

// ---- control and data sockets -------------------------------------------------------------
int ShimTcpReceive (CSocket *pSocket, void *pBuffer, unsigned nLength)
{
	if (pSocket != g_pControl)
	{
		// data connection (STOR): one chunk, then end of data
		bool &bSent = g_DataSent[pSocket];
		if (bSent)
			return -1;
		bSent = true;
		static const char Chunk[] = "Volume1=99\r\n";
		unsigned n = sizeof Chunk - 1 < nLength ? sizeof Chunk - 1 : nLength;
		memcpy (pBuffer, Chunk, n);
		return (int) n;
	}
	if (g_nPos >= g_nSize)
		return -1;
	// One command line per receive, terminated by CRLF as RFC 959 requires of the client: the
	// worker strips the terminator by writing m_CommandBuffer[nReceiveBytes - 2] (upstream
	// "FIXME"), so a shorter segment would write before the buffer (see the PR's findings).
	const uint8_t *p = g_pData + g_nPos;
	const void *pEnd = memchr (p, '\n', g_nSize - g_nPos);
	size_t nLine = pEnd ? (const uint8_t *) pEnd - p : g_nSize - g_nPos;
	g_nPos += nLine + (pEnd ? 1 : 0);
	if (nLine && p[nLine - 1] == '\r')
		nLine--;
	std::string Line ((const char *) p, nLine);
	Line += "\r\n";
	size_t n = Line.size () < nLength ? Line.size () : nLength;
	memcpy (pBuffer, Line.data (), n);
	return (int) n;
}

int ShimTcpSend (CSocket *pSocket, const void *pBuffer, unsigned nLength)
{
	const uint8_t *p = (const uint8_t *) pBuffer;
	std::string &Out = pSocket == g_pControl ? g_FtpControlOut : g_FtpDataOut;
	if (Out.size () < 65536)
		Out.append ((const char *) p, nLength);
	for (unsigned i = 0; i < nLength; i++)
		g_Sink += p[i];
	return (int) nLength;
}

int ShimTcpConnect (CSocket *, const CIPAddress &, u16) { return 0; }

CSocket *ShimTcpAccept (CSocket *) { return new CSocket (CNetSubSystem::Get (), IPPROTO_TCP); }

void ShimSchedulerYield (void) { g_ShimClockTicks += 100000; }	// 100 ms per task switch

// ---- CFTPWorker's other collaborators ------------------------------------------------------
boolean CmDNSPublisher::UnpublishService (const char *) { return TRUE; }
boolean CmDNSPublisher::UnpublishService (const char *, const char *, u16) { return TRUE; }
// Only reached with a config object; the session passes none (src/config.cpp is not built).
const char *CConfig::GetNetworkHostname (void) const { return "MiniDexed"; }

// ---- in-memory FatFs ------------------------------------------------------------------------
extern "C" {
FRESULT f_opendir (DIR *, const TCHAR *pPath)
{
	auto it = g_Files.find (Key (pPath));
	return it != g_Files.end () && it->second.bDir ? FR_OK : FR_NO_PATH;
}
FRESULT f_closedir (DIR *) { return FR_OK; }
FRESULT f_findnext (DIR *, FILINFO *pInfo)
{
	memset (pInfo, 0, sizeof *pInfo);
	if (g_nListPos >= g_Listing.size ())
		return FR_OK;			// fname[0] == 0: end of directory
	const std::string &k = g_Listing[g_nListPos++];
	const TEntry &e = g_Files[k];
	std::string Name = k.substr (k.rfind ('/') + 1);
	strncpy (pInfo->fname, Name.c_str (), sizeof pInfo->fname - 1);
	pInfo->fattrib = e.bDir ? AM_DIR : 0;
	pInfo->fsize = e.Data.size ();
	pInfo->fdate = (45 << 9) | (6 << 5) | 15;	// 2025-06-15
	pInfo->ftime = (12 << 11) | (34 << 5);
	return FR_OK;
}
FRESULT f_findfirst (DIR *pDir, FILINFO *pInfo, const TCHAR *pPath, const TCHAR *)
{
	std::string Dir = Key (pPath);
	auto it = g_Files.find (Dir);
	if (it == g_Files.end () || !it->second.bDir)
		return FR_NO_PATH;
	g_Listing.clear ();
	g_nListPos = 0;
	for (const auto &f : g_Files)
		if (f.first != Dir && Parent (f.first) == Dir)
			g_Listing.push_back (f.first);
	return f_findnext (pDir, pInfo);
}
FRESULT f_stat (const TCHAR *pPath, FILINFO *pInfo)
{
	auto it = g_Files.find (Key (pPath));
	if (it == g_Files.end ())
		return FR_NO_FILE;
	if (pInfo)
	{
		memset (pInfo, 0, sizeof *pInfo);
		pInfo->fattrib = it->second.bDir ? AM_DIR : 0;
		pInfo->fsize = it->second.Data.size ();
	}
	return FR_OK;
}
FRESULT f_open (FIL *pFile, const TCHAR *pPath, BYTE nMode)
{
	std::string k = Key (pPath);
	memset (pFile, 0, sizeof *pFile);
	auto it = g_Files.find (k);
	if (nMode & FA_CREATE_ALWAYS)
	{
		auto p = g_Files.find (Parent (k));
		if (p == g_Files.end () || !p->second.bDir || (it != g_Files.end () && it->second.bDir))
			return FR_NO_PATH;
		g_Files[k] = {false, ""};
	}
	else if (it == g_Files.end () || it->second.bDir)
		return FR_NO_FILE;
	g_OpenFile = k;
	g_nFilePos = 0;
	pFile->obj.objsize = g_Files[k].Data.size ();
	return FR_OK;
}
FRESULT f_read (FIL *, void *pBuffer, UINT nBytes, UINT *pRead)
{
	const std::string &d = g_Files[g_OpenFile].Data;
	size_t n = g_nFilePos < d.size () ? d.size () - g_nFilePos : 0;
	if (n > nBytes)
		n = nBytes;
	memcpy (pBuffer, d.data () + g_nFilePos, n);
	g_nFilePos += n;
	*pRead = (UINT) n;
	return FR_OK;
}
FRESULT f_write (FIL *, const void *pBuffer, UINT nBytes, UINT *pWritten)
{
	std::string &d = g_Files[g_OpenFile].Data;
	if (d.size () + nBytes > 65536)
		return FR_DENIED;
	d.append ((const char *) pBuffer, nBytes);
	*pWritten = nBytes;
	return FR_OK;
}
FRESULT f_sync (FIL *) { return FR_OK; }
FRESULT f_close (FIL *) { g_OpenFile.clear (); return FR_OK; }
FRESULT f_unlink (const TCHAR *pPath)
{
	std::string k = Key (pPath);
	auto it = g_Files.find (k);
	if (it == g_Files.end () || Parent (k).empty ())
		return FR_NO_FILE;
	for (const auto &f : g_Files)
		if (Parent (f.first) == k)
			return FR_DENIED;	// directory not empty
	g_Files.erase (it);
	return FR_OK;
}
FRESULT f_mkdir (const TCHAR *pPath)
{
	std::string k = Key (pPath);
	auto p = g_Files.find (Parent (k));
	if (p == g_Files.end () || !p->second.bDir)
		return FR_NO_PATH;
	if (g_Files.count (k))
		return FR_EXIST;
	g_Files[k] = {true, ""};
	return FR_OK;
}
FRESULT f_rename (const TCHAR *pOld, const TCHAR *pNew)
{
	std::string o = Key (pOld), n = Key (pNew);
	auto it = g_Files.find (o);
	if (it == g_Files.end () || Parent (o).empty ())
		return FR_NO_FILE;
	auto p = g_Files.find (Parent (n));
	if (p == g_Files.end () || !p->second.bDir)
		return FR_NO_PATH;
	if (g_Files.count (n))
		return FR_EXIST;
	if (it->second.bDir)
		for (const auto &f : g_Files)
			if (Parent (f.first) == o)
				return FR_DENIED;	// keep the stand-in simple: only empty dirs move
	TEntry e = it->second;
	g_Files.erase (it);
	g_Files[n] = e;
	return FR_OK;
}
}


void FtpSession (const uint8_t *pData, size_t nSize)
{
	g_pData = pData;
	g_nSize = nSize;
	g_nPos = 0;
	g_ShimClockTicks = 1000000;
	g_DataSent.clear ();
	ResetFiles ();
	g_Listing.clear ();
	g_OpenFile.clear ();
	g_FtpControlOut.clear ();
	g_FtpDataOut.clear ();

	g_pControl = new CSocket (CNetSubSystem::Get (), IPPROTO_TCP);
	// No mDNS publisher / config: the worker then skips the unpublish step before reboot ().
	CFTPWorker *pWorker = new CFTPWorker (g_pControl, "admin", "minidexed", nullptr, nullptr);
	try
	{
		pWorker->Run ();
	}
	catch (const CShimReboot &)
	{
	}
	delete pWorker;
	g_pControl = nullptr;
	for (const char *p = g_FtpDataOut.data (), *e = p + g_FtpDataOut.size (); p < e; p++)
		g_Sink += (uint8_t) *p;
}
