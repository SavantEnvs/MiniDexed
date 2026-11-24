// libFuzzer harness: performance bank / performance file discovery on the SD card
// (src/performanceconfig.cpp: Init -> ListPerformanceBanks / ListPerformances, bank and
// performance selection, name lookup, new-performance creation).
//
// The fuzz input describes the SD card's "performance" directory as newline-separated entries:
//   [dir:1][attr:1][name bytes...]'\n'
//     dir   0 = entry of SD:/performance; N > 0 = entry of the N-th directory entry of
//           SD:/performance (a performance bank directory such as "002_My Bank")
//     attr  FatFs attribute bits (AM_DIR, AM_HID, AM_SYS, ...)
// An in-memory FatFs stand-in (below) serves these listings through f_opendir/f_findfirst/
// f_findnext, matching "*" and "*.ini" patterns like FatFs, and discards writes. Names are
// truncated to FatFs's long-file-name limit. Per-input state: config object and listing are
// rebuilt for every input.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <fatfs/ff.h>
#include "performanceconfig.h"

namespace {
struct TEntry { unsigned nDir; BYTE nAttr; std::string Name; };
std::vector<TEntry> g_Entries;
std::vector<std::string> g_RootDirs;		// names of AM_DIR entries of SD:/performance
const char Root[] = "SD:/performance";
// the open listing (the code under test has at most one open at a time)
int g_nListDir = -1;
size_t g_nListPos;
std::string g_Pattern;
volatile uint32_t g_Sink;

bool Match (const std::string &Name)
{
	if (g_Pattern == "*")
		return true;
	if (g_Pattern.size () > 1 && g_Pattern[0] == '*')
	{
		const std::string Ext = g_Pattern.substr (1);
		return Name.size () >= Ext.size ()
		    && strcasecmp (Name.c_str () + Name.size () - Ext.size (), Ext.c_str ()) == 0;
	}
	return strcasecmp (Name.c_str (), g_Pattern.c_str ()) == 0;
}

int DirOf (const char *pPath)
{
	std::string Path (pPath);
	while (Path.size () > strlen (Root) && Path.back () == '/')
		Path.pop_back ();
	if (Path == Root)
		return 0;
	if (Path.compare (0, strlen (Root) + 1, std::string (Root) + "/") != 0)
		return -1;
	std::string Sub = Path.substr (strlen (Root) + 1);
	for (size_t i = 0; i < g_RootDirs.size (); i++)
		if (g_RootDirs[i] == Sub)
			return (int) i + 1;
	return -1;
}

FRESULT Next (FILINFO *pInfo)
{
	for (; g_nListPos < g_Entries.size (); g_nListPos++)
	{
		const TEntry &E = g_Entries[g_nListPos];
		if ((int) E.nDir == g_nListDir && Match (E.Name))
		{
			strncpy (pInfo->fname, E.Name.c_str (), sizeof pInfo->fname - 1);
			pInfo->fname[sizeof pInfo->fname - 1] = '\0';
			pInfo->fattrib = E.nAttr;
			pInfo->fsize = 0;
			g_nListPos++;
			return FR_OK;
		}
	}
	pInfo->fname[0] = '\0';
	return FR_OK;
}
} // namespace

extern "C" {
FRESULT f_opendir (DIR *, const TCHAR *pPath)
{
	return DirOf (pPath) >= 0 ? FR_OK : FR_NO_PATH;
}
FRESULT f_closedir (DIR *) { g_nListDir = -1; return FR_OK; }
FRESULT f_findfirst (DIR *, FILINFO *pInfo, const TCHAR *pPath, const TCHAR *pPattern)
{
	g_nListDir = DirOf (pPath);
	if (g_nListDir < 0)
		return FR_NO_PATH;
	g_nListPos = 0;
	g_Pattern = pPattern;
	return Next (pInfo);
}
FRESULT f_findnext (DIR *, FILINFO *pInfo) { return Next (pInfo); }
FRESULT f_open (FIL *, const TCHAR *pPath, BYTE) { g_Sink += strlen (pPath); return FR_OK; }
FRESULT f_read (FIL *, void *, UINT, UINT *pRead) { *pRead = 0; return FR_OK; }
FRESULT f_write (FIL *, const void *pBuffer, UINT nBytes, UINT *pWritten)
{
	const uint8_t *p = (const uint8_t *) pBuffer;
	for (UINT i = 0; i < nBytes; i++)
		g_Sink += p[i];
	*pWritten = nBytes;
	return FR_OK;
}
FRESULT f_close (FIL *) { return FR_OK; }
FRESULT f_unlink (const TCHAR *pPath) { g_Sink += strlen (pPath); return FR_OK; }
FRESULT f_mkdir (const TCHAR *pPath) { g_Sink += strlen (pPath); return FR_OK; }
FRESULT f_stat (const TCHAR *, FILINFO *) { return FR_NO_FILE; }
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *pData, size_t nSize)
{
	g_Entries.clear ();
	g_RootDirs.clear ();
	g_nListDir = -1;
	size_t i = 0;
	while (i + 2 <= nSize)
	{
		TEntry E;
		E.nDir = pData[i];
		E.nAttr = pData[i + 1];
		i += 2;
		while (i < nSize && pData[i] != '\n')
		{
			if (pData[i] != '\0' && E.Name.size () < FF_MAX_LFN)
				E.Name += (char) pData[i];
			i++;
		}
		i++;
		if (E.Name.empty ())
			continue;
		if (E.nDir == 0 && (E.nAttr & AM_DIR))
			g_RootDirs.push_back (E.Name);
		g_Entries.push_back (E);
	}

	FATFS FileSystem;
	CPerformanceConfig *pConfig = new CPerformanceConfig (&FileSystem);
	pConfig->Init (CConfig::AllToneGenerators);
	for (unsigned nBank = 0; nBank < NUM_PERFORMANCE_BANKS; nBank++)
	{
		g_Sink += pConfig->GetPerformanceBankName (nBank).size ();
		if (!pConfig->IsValidPerformanceBank (nBank))
			continue;
		pConfig->SetNewPerformanceBank (nBank);
		for (unsigned nID = 0; nID < NUM_PERFORMANCES; nID++)
		{
			if (!pConfig->IsValidPerformance (nID))
				continue;
			g_Sink += pConfig->GetPerformanceName (nID).size ()
				+ pConfig->GetPerformanceFileName (nID).size ()
				+ pConfig->GetPerformanceFullFilePath (nID).size ();
		}
		g_Sink += pConfig->GetLastPerformance ();
		if (pConfig->CheckFreePerformanceSlot ())
		{
			g_Sink += pConfig->GetNewPerformanceDefaultName ().size ();
			pConfig->CreateNewPerformanceFile ();
		}
	}
	g_Sink += pConfig->GetLastPerformanceBank ();
	delete pConfig;
	return 0;
}
