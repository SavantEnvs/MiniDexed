// libFuzzer harness: DX7 voice-bank (.syx) loading from the SD card (src/sysexfileloader.cpp).
//
// The fuzz input is one SD-card file plus its name:
//   [flags:1][namelen:1][name:namelen][file contents...]
//     flags bit0  bHeaderlessSysExVoices (minidexed.ini option HeaderlessSysExVoices)
//     flags bit1  place the file in a subdirectory of sysex/voice (subdir recursion path)
// The file is written to a fresh private directory <tmp>/sysex/voice[/sub]/<name>, then the real
// CSysExFileLoader::Load() scans it (filename bank-number parsing, header/size checks), and every
// public accessor the UI and MIDI program-change paths use is called for every loaded bank and
// voice (GetBankName / GetVoiceName / GetVoice -> DecodePackedVoice / next-bank navigation).
// FAT file names cannot contain '/' or NUL, so those bytes end the name (filesystem precondition).
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <set>
#include <new>
#include <string>
#include "sysexfileloader.h"

static std::string g_Root;
// The loader is ~650 KB (16384 bank slots). It is constructed fresh for every input, but in the
// same static storage: a new heap block per input made ASan's large-allocation path grow RSS
// without bound (OOM after ~4k execs), although every loader was deleted.
alignas (CSysExFileLoader) static unsigned char g_LoaderStorage[sizeof (CSysExFileLoader)];
static volatile uint8_t g_Sink;

// CSysExFileLoader::LoadBank() returns without closedir() when it hits the subdirectory depth
// limit, and it recurses into "." and ".." entries, so every Load() leaks DIR handles (memory and
// file descriptors). In a persistent fuzzer that state would carry over and exhaust memory/fds
// after a few thousand inputs. build.sh links the loader with -Wl,--wrap=opendir,--wrap=closedir
// so the harness can close, after each input, the handles that input left open (per-input reset
// of process state; the loader's own code is unchanged). The scanned tree is nested four levels
// below the private scratch dir so the ".." recursion never leaves it.
static std::set<DIR *> g_OpenDirs;
extern "C" DIR *__real_opendir (const char *);
extern "C" int __real_closedir (DIR *);
extern "C" DIR *__wrap_opendir (const char *pName)
{
	DIR *d = __real_opendir (pName);
	if (d)
		g_OpenDirs.insert (d);
	return d;
}
extern "C" int __wrap_closedir (DIR *d)
{
	g_OpenDirs.erase (d);
	return __real_closedir (d);
}

extern "C" int LLVMFuzzerInitialize (int *, char ***)
{
	const char *pTmp = getenv ("TMPDIR");
	std::string Templ = std::string (pTmp && *pTmp ? pTmp : "/tmp") + "/minidexed-syx-XXXXXX";
	char *p = mkdtemp (&Templ[0]);
	if (!p)
		abort ();
	g_Root = p;
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *pData, size_t nSize)
{
	if (nSize < 2)
		return -1;
	const bool bHeaderless = pData[0] & 1;
	const bool bSubDir = pData[0] & 2;
	size_t nNameLen = pData[1];
	if (nNameLen > nSize - 2)
		return -1;
	std::string Name;
	for (size_t i = 0; i < nNameLen; i++)
	{
		char c = (char) pData[2 + i];
		if (c == '/' || c == '\0')
			break;
		Name += c;
	}
	if (Name.empty () || Name == "." || Name == "..")
		return -1;
	const uint8_t *pFile = pData + 2 + nNameLen;
	size_t nFile = nSize - 2 - nNameLen;

	std::string Base = g_Root + "/a/b/c";
	mkdir ((g_Root + "/a").c_str (), 0700);
	mkdir ((g_Root + "/a/b").c_str (), 0700);
	mkdir (Base.c_str (), 0700);
	std::string Sysex = Base + "/sysex";
	std::string Voice = Sysex + "/voice";
	std::string Dir = bSubDir ? Voice + "/sub" : Voice;
	mkdir (Sysex.c_str (), 0700);
	mkdir (Voice.c_str (), 0700);
	mkdir (Dir.c_str (), 0700);
	std::string Path = Dir + "/" + Name;
	FILE *f = fopen (Path.c_str (), "wb");
	if (!f)
		return -1;	// name not representable on the host filesystem (e.g. too long)
	if (nFile)
		fwrite (pFile, 1, nFile, f);
	fclose (f);

	CSysExFileLoader *pLoader = new (g_LoaderStorage) CSysExFileLoader (Sysex.c_str ());
	pLoader->Load (bHeaderless);

	unsigned nHighest = pLoader->GetNumHighestBank ();
	for (unsigned nBank = 0; nBank <= nHighest; nBank++)
	{
		if (!pLoader->IsValidBank (nBank))
			continue;
		std::string BankName = pLoader->GetBankName (nBank);
		g_Sink ^= (uint8_t) BankName.size ();
		for (unsigned nVoice = 0; nVoice < CSysExFileLoader::VoicesPerBank; nVoice++)
		{
			uint8_t Decoded[CSysExFileLoader::SizeSingleVoice];
			pLoader->GetVoice (nBank, nVoice, Decoded);
			for (uint8_t b : Decoded)
				g_Sink ^= b;
			g_Sink ^= (uint8_t) pLoader->GetVoiceName (nBank, nVoice).size ();
		}
		g_Sink ^= (uint8_t) pLoader->GetNextBankUp (nBank);
		g_Sink ^= (uint8_t) pLoader->GetNextBankDown (nBank);
	}
	pLoader->~CSysExFileLoader ();
	for (DIR *d : g_OpenDirs)
		__real_closedir (d);
	g_OpenDirs.clear ();

	unlink (Path.c_str ());
	rmdir (Dir.c_str ());
	rmdir (Voice.c_str ());
	return 0;
}
