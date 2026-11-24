// libFuzzer harness: performance file loading from the SD card (src/performanceconfig.cpp on top
// of Circle's Properties .ini parser, addon/Properties).
//
// The fuzz input is the content of the performance file. A small in-memory FatFs stand-in
// (below) serves it for f_open()/f_read() of any file name and discards writes. The harness then
// does what CMiniDexed does with a performance: CPerformanceConfig::Load(), every per-TG getter,
// VoiceDataFilled()/GetVoiceDataFromTxt() (the hex voice dump stored in the file), the effect
// getters, and Save() (the Properties writer, output discarded).
// Per-input state: the config object is created and destroyed per input; the file is the input.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fatfs/ff.h>
#include "performanceconfig.h"

static const uint8_t *g_pFile;
static size_t g_nFile, g_nPos;
static volatile uint32_t g_Sink;

extern "C" {
FRESULT f_open (FIL *, const TCHAR *, BYTE nMode)
{
	if (nMode & FA_READ)
	{
		g_nPos = 0;
	}
	return FR_OK;
}
FRESULT f_read (FIL *, void *pBuffer, UINT nBytes, UINT *pRead)
{
	size_t n = g_nFile - g_nPos < nBytes ? g_nFile - g_nPos : nBytes;
	memcpy (pBuffer, g_pFile + g_nPos, n);
	g_nPos += n;
	*pRead = (UINT) n;
	return FR_OK;
}
FRESULT f_write (FIL *, const void *pBuffer, UINT nBytes, UINT *pWritten)
{
	const uint8_t *p = (const uint8_t *) pBuffer;
	for (UINT i = 0; i < nBytes; i++)
		g_Sink += p[i];
	*pWritten = nBytes;
	return FR_OK;
}
FRESULT f_close (FIL *) { return FR_OK; }
FRESULT f_opendir (DIR *, const TCHAR *) { return FR_NO_PATH; }
FRESULT f_closedir (DIR *) { return FR_OK; }
FRESULT f_findfirst (DIR *, FILINFO *, const TCHAR *, const TCHAR *) { return FR_NO_FILE; }
FRESULT f_findnext (DIR *, FILINFO *) { return FR_NO_FILE; }
FRESULT f_unlink (const TCHAR *) { return FR_NO_FILE; }
FRESULT f_mkdir (const TCHAR *) { return FR_DENIED; }
FRESULT f_stat (const TCHAR *, FILINFO *) { return FR_NO_FILE; }
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *pData, size_t nSize)
{
	g_pFile = pData;
	g_nFile = nSize;
	g_nPos = 0;

	FATFS FileSystem;
	CPerformanceConfig *pConfig = new CPerformanceConfig (&FileSystem);
	if (pConfig->Load ())
	{
		for (unsigned nTG = 0; nTG < CConfig::AllToneGenerators; nTG++)
		{
			g_Sink += pConfig->GetBankNumber (nTG) + pConfig->GetVoiceNumber (nTG)
				+ pConfig->GetMIDIChannel (nTG) + pConfig->GetVolume (nTG) + pConfig->GetPan (nTG)
				+ pConfig->GetDetune (nTG) + pConfig->GetCutoff (nTG) + pConfig->GetResonance (nTG)
				+ pConfig->GetNoteLimitLow (nTG) + pConfig->GetNoteLimitHigh (nTG)
				+ pConfig->GetNoteShift (nTG) + pConfig->GetReverbSend (nTG)
				+ pConfig->GetPitchBendRange (nTG) + pConfig->GetPitchBendStep (nTG)
				+ pConfig->GetPortamentoMode (nTG) + pConfig->GetPortamentoGlissando (nTG)
				+ pConfig->GetPortamentoTime (nTG) + pConfig->GetMonoMode (nTG)
				+ pConfig->GetModulationWheelRange (nTG) + pConfig->GetModulationWheelTarget (nTG)
				+ pConfig->GetFootControlRange (nTG) + pConfig->GetFootControlTarget (nTG)
				+ pConfig->GetBreathControlRange (nTG) + pConfig->GetBreathControlTarget (nTG)
				+ pConfig->GetAftertouchRange (nTG) + pConfig->GetAftertouchTarget (nTG);
			if (pConfig->VoiceDataFilled (nTG))
			{
				const uint8_t *pVoice = pConfig->GetVoiceDataFromTxt (nTG);
				for (unsigned i = 0; i < NUM_VOICE_PARAM; i++)
					g_Sink += pVoice[i];
			}
		}
		g_Sink += pConfig->GetCompressorEnable () + pConfig->GetReverbEnable ()
			+ pConfig->GetReverbSize () + pConfig->GetReverbHighDamp ()
			+ pConfig->GetReverbLowDamp () + pConfig->GetReverbLowPass ()
			+ pConfig->GetReverbDiffusion () + pConfig->GetReverbLevel ();
		pConfig->Init (CConfig::AllToneGenerators);	// tone-generator count used by Save()
		pConfig->Save ();
	}
	delete pConfig;
	return 0;
}
