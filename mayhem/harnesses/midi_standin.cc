// Recording stand-ins for CMiniDexed, CUserInterface and the CConfig getters that the MIDI
// input path (src/mididevice.cpp, src/serialmididevice.cpp) calls; declarations in
// mayhem/shim/midi/standin.h, which is force-included. Shared by fuzz_serial_midi and the oracle.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mididevice.h"
#include "config.h"

std::vector<std::string> g_StandinCalls;
bool g_StandinSysExAccept = false;
int16_t g_StandinSysExReject = -1;
unsigned g_StandinPerformanceSelectChannel = CMIDIDevice::Disabled;
TStandinConfig g_StandinConfig = {CConfig::AllToneGenerators, false, true, false, false, false,
				  "", "", "", "", 0, 0, 0, 0};

static void Rec (const char *pFormat, ...) __attribute__ ((format (printf, 1, 2)));
static void Rec (const char *pFormat, ...)
{
	char Buf[64];
	va_list ap;
	va_start (ap, pFormat);
	vsnprintf (Buf, sizeof Buf, pFormat, ap);
	va_end (ap);
	g_StandinCalls.emplace_back (Buf);
}

// ---- CMiniDexed --------------------------------------------------------------------------
#define REC1(name, T)        void CMiniDexed::name (T a) { Rec (#name "(%d)", (int) a); }
#define REC2(name, T1, T2)   void CMiniDexed::name (T1 a, T2 b) { Rec (#name "(%d,%d)", (int) a, (int) b); }
REC2 (BankSelectMSB, unsigned, unsigned)
REC1 (BankSelectMSBPerformance, unsigned)
REC2 (BankSelectLSB, unsigned, unsigned)
REC1 (BankSelectLSBPerformance, unsigned)
REC2 (ProgramChange, unsigned, unsigned)
REC1 (ProgramChangePerformance, unsigned)
REC2 (SetVolume, unsigned, unsigned)
REC2 (SetExpression, unsigned, unsigned)
REC2 (SetPan, unsigned, unsigned)
REC2 (SetMasterTune, int, unsigned)
REC2 (SetCutoff, int, unsigned)
REC2 (SetResonance, int, unsigned)
REC2 (keyup, int16_t, unsigned)
REC2 (setSustain, bool, unsigned)
REC2 (setSostenuto, bool, unsigned)
REC2 (setHoldMode, bool, unsigned)
REC2 (panic, uint8_t, unsigned)
REC2 (notesOff, uint8_t, unsigned)
REC2 (setModWheel, uint8_t, unsigned)
REC2 (setPitchbend, int16_t, unsigned)
REC1 (ControllersRefresh, unsigned)
REC2 (setFootController, uint8_t, unsigned)
REC2 (setBreathController, uint8_t, unsigned)
REC2 (setAftertouch, uint8_t, unsigned)
REC2 (SetReverbSend, unsigned, unsigned)
REC2 (setMonoMode, uint8_t, uint8_t)
REC2 (setPitchbendRange, uint8_t, uint8_t)
REC2 (setPitchbendStep, uint8_t, uint8_t)
REC2 (setPortamentoMode, uint8_t, uint8_t)
REC2 (setPortamentoGlissando, uint8_t, uint8_t)
REC2 (setPortamentoTime, uint8_t, uint8_t)
REC2 (setModWheelRange, uint8_t, uint8_t)
REC2 (setModWheelTarget, uint8_t, uint8_t)
REC2 (setFootControllerRange, uint8_t, uint8_t)
REC2 (setFootControllerTarget, uint8_t, uint8_t)
REC2 (setBreathControllerRange, uint8_t, uint8_t)
REC2 (setBreathControllerTarget, uint8_t, uint8_t)
REC2 (setAftertouchRange, uint8_t, uint8_t)
REC2 (setAftertouchTarget, uint8_t, uint8_t)
REC2 (setOPMask, uint8_t, uint8_t)

void CMiniDexed::keydown (int16_t pitch, uint8_t velocity, unsigned nTG)
{
	Rec ("keydown(%d,%d,%u)", pitch, velocity, nTG);
}

void CMiniDexed::setVoiceDataElement (uint8_t data, uint8_t number, uint8_t nTG)
{
	Rec ("setVoiceDataElement(%d,%d,%d)", data, number, nTG);
}

void CMiniDexed::setMasterVolume (float32_t vol)
{
	Rec ("setMasterVolume(%.4f)", (double) vol);
}

unsigned CMiniDexed::GetPerformanceSelectChannel (void)
{
	return g_StandinPerformanceSelectChannel;
}

// Upstream: if (nTG >= m_nToneGenerators) return; then SetChannel () on every MIDI device.
void CMiniDexed::SetMIDIChannel (uint8_t uchChannel, unsigned nTG)
{
	Rec ("SetMIDIChannel(%d,%u)", uchChannel, nTG);
	if (nTG >= g_StandinConfig.nToneGenerators)
		return;
	if (m_pDevice)
		m_pDevice->SetChannel (uchChannel, nTG);
}

// Upstream copies the first 161 bytes of the message (6-byte header + 155 voice bytes) and
// loads them into the TG. Read the same bytes here, so a short buffer is caught like upstream.
void CMiniDexed::loadVoiceParameters (const uint8_t *data, uint8_t nTG)
{
	if (nTG >= g_StandinConfig.nToneGenerators)
		return;
	uint8_t voice[161];
	memcpy (voice, data, sizeof voice);
	unsigned nSum = 0;
	for (unsigned i = 6; i < sizeof voice; i++)
		nSum += voice[i];
	Rec ("loadVoiceParameters(%u,%d)", nSum, nTG);
}

// Upstream: F0 43 0n 00 01 1B <155 voice bytes> <checksum> F7 (163 bytes).
void CMiniDexed::getSysExVoiceDump (uint8_t *dest, uint8_t nTG)
{
	static const uint8_t Head[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
	memcpy (dest, Head, sizeof Head);
	dest[2] = 0x00 | (nTG & 0x0F);
	memset (dest + 6, 0, 155);
	dest[161] = 0;
	dest[162] = 0xF7;
	Rec ("getSysExVoiceDump(%d)", nTG);
}

// Upstream forwards to Synth_Dexed's Dexed::checkSystemExclusive () (not built: it needs the
// whole Synth_Dexed engine). This keeps that function's contract - which message length can
// yield which result - and lets the harness pick, per input, whether the message is accepted
// (g_StandinSysExAccept) or which error code it is rejected with, so every branch of
// CMIDIDevice::HandleSystemExclusive () is reachable without inventing impossible results:
//   7 bytes      parameter change: 300 + parameter number (455 = operator enable mask)
//   163 bytes    single-voice dump: 100
//   4104 bytes   32-voice bank dump: 200
//   5 bytes      voice dump request: 500 + voice number
// Results are read only after upstream's own "inactive TG" check (returns 0).
int16_t CMiniDexed::checkSystemExclusive (const uint8_t *pMessage, const uint16_t nLength, uint8_t nTG)
{
	Rec ("checkSystemExclusive(%u,%d)", nLength, nTG);
	if (nTG >= g_StandinConfig.nToneGenerators)
		return 0;
	if (!g_StandinSysExAccept)
		return g_StandinSysExReject;
	switch (nLength)
	{
	case 7: {
		unsigned nParam = pMessage[4] + ((pMessage[3] & 0x03) * 128);
		return nParam <= 155 ? 300 + nParam : -11; }
	case 163:	return 100;
	case 4104:	return 200;
	case 5:		return 500 + (pMessage[3] & 0x1F);
	default:	return -11;
	}
}

// ---- CUserInterface ----------------------------------------------------------------------
void CUserInterface::UIMIDICmdHandler (unsigned nMidiCh, unsigned nMidiType, unsigned nMidiData1, unsigned nMidiData2)
{
	Rec ("UI(%u,%u,%u,%u)", nMidiCh, nMidiType, nMidiData1, nMidiData2);
}

// ---- CConfig (constructor and the getters mididevice.cpp reads) ----------------------------
CConfig::CConfig (FATFS *pFileSystem) : m_Properties ("minidexed.ini", pFileSystem) {}
CConfig::~CConfig (void) {}
unsigned CConfig::GetToneGenerators (void) const { return g_StandinConfig.nToneGenerators; }
bool CConfig::GetMIDIDumpEnabled (void) const { return g_StandinConfig.bMIDIDump; }
bool CConfig::GetMIDIRXProgramChange (void) const { return g_StandinConfig.bRXProgramChange; }
bool CConfig::GetIgnoreAllNotesOff (void) const { return g_StandinConfig.bIgnoreAllNotesOff; }
bool CConfig::GetMIDIThruIgnoreClock (void) const { return g_StandinConfig.bThruIgnoreClock; }
bool CConfig::GetMIDIThruIgnoreActiveSensing (void) const { return g_StandinConfig.bThruIgnoreActiveSensing; }
const char *CConfig::GetMIDIThruIn (void) const { return g_StandinConfig.ThruIn.c_str (); }
const char *CConfig::GetMIDIThruOut (void) const { return g_StandinConfig.ThruOut.c_str (); }
const char *CConfig::GetMIDIThru2In (void) const { return g_StandinConfig.Thru2In.c_str (); }
const char *CConfig::GetMIDIThru2Out (void) const { return g_StandinConfig.Thru2Out.c_str (); }
unsigned CConfig::GetMIDISystemCCVol (void) const { return g_StandinConfig.nCCVol; }
unsigned CConfig::GetMIDISystemCCPan (void) const { return g_StandinConfig.nCCPan; }
unsigned CConfig::GetMIDISystemCCDetune (void) const { return g_StandinConfig.nCCDetune; }
unsigned CConfig::GetMIDIGlobalExpression (void) const { return g_StandinConfig.nGlobalExpression; }
unsigned CConfig::GetMIDIBaudRate (void) const { return 31250; }

// The CPropertiesFatFsFile member is constructed but never loaded: no SD card on the host.
extern "C" {
FRESULT f_open (FIL *, const TCHAR *, BYTE) { return FR_NO_FILE; }
FRESULT f_read (FIL *, void *, UINT, UINT *pRead) { *pRead = 0; return FR_DISK_ERR; }
FRESULT f_write (FIL *, const void *, UINT, UINT *pWritten) { *pWritten = 0; return FR_DENIED; }
FRESULT f_close (FIL *) { return FR_OK; }
}
