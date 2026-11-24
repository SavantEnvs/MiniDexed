// Force-included (-include) when compiling src/mididevice.cpp, src/serialmididevice.cpp and the
// serial-MIDI harness/test. It stands in for the two classes the MIDI input path calls into but
// that cannot be built for the host:
//   CMiniDexed      (src/minidexed.h: the whole synth - Synth_Dexed, CMSIS, sound, WLAN, GPIO ...)
//   CUserInterface  (src/userinterface.h: LCD/OLED/TFT displays, rotary encoder, GPIO buttons)
// Defining their include guards here makes the real headers expand to nothing; the classes below
// declare exactly the methods src/mididevice.cpp calls, with upstream's signatures, and record
// each call (mayhem/harnesses/midi_standin.cc) so the oracle can assert what the MIDI parser
// dispatched. src/mididevice.cpp and src/serialmididevice.cpp themselves are compiled unmodified.
#ifndef _mayhem_midi_standin_h
#define _mayhem_midi_standin_h
#define _minidexed_h
#define _userinterface_h
#include <stdint.h>
#include <string>
#include <vector>
#include <circle/types.h>
typedef float float32_t;		// CMSIS arm_math type, used by common.h and mididevice.cpp
#include "common.h"			// maplong (), included by the real minidexed.h

class CMIDIDevice;

// Every stand-in call appends one "name(arg,arg,...)" line here.
extern std::vector<std::string> g_StandinCalls;
// Verdict of the stand-in checkSystemExclusive () (upstream forwards to Synth_Dexed's
// Dexed::checkSystemExclusive (); see midi_standin.cc).
extern bool g_StandinSysExAccept;
extern int16_t g_StandinSysExReject;		// -1 .. -11
extern unsigned g_StandinPerformanceSelectChannel;

class CMiniDexed
{
public:
	void RegisterMIDIDevice (CMIDIDevice *pDevice) { m_pDevice = pDevice; }	// stand-in only

	void BankSelectMSB (unsigned nBankMSB, unsigned nTG);
	void BankSelectMSBPerformance (unsigned nBankMSB);
	void BankSelectLSB (unsigned nBankLSB, unsigned nTG);
	void BankSelectLSBPerformance (unsigned nBankLSB);
	void ProgramChange (unsigned nProgram, unsigned nTG);
	void ProgramChangePerformance (unsigned nProgram);
	void SetVolume (unsigned nVolume, unsigned nTG);
	void SetExpression (unsigned nExpression, unsigned nTG);
	void SetPan (unsigned nPan, unsigned nTG);
	void SetMasterTune (int nMasterTune, unsigned nTG);
	void SetCutoff (int nCutoff, unsigned nTG);
	void SetResonance (int nResonance, unsigned nTG);
	void SetMIDIChannel (uint8_t uchChannel, unsigned nTG);
	void keyup (int16_t pitch, unsigned nTG);
	void keydown (int16_t pitch, uint8_t velocity, unsigned nTG);
	void setSustain (bool sustain, unsigned nTG);
	void setSostenuto (bool sostenuto, unsigned nTG);
	void setHoldMode (bool holdmode, unsigned nTG);
	void panic (uint8_t value, unsigned nTG);
	void notesOff (uint8_t value, unsigned nTG);
	void setModWheel (uint8_t value, unsigned nTG);
	void setPitchbend (int16_t value, unsigned nTG);
	void ControllersRefresh (unsigned nTG);
	void setFootController (uint8_t value, unsigned nTG);
	void setBreathController (uint8_t value, unsigned nTG);
	void setAftertouch (uint8_t value, unsigned nTG);
	void SetReverbSend (unsigned nReverbSend, unsigned nTG);
	void setMonoMode (uint8_t mono, uint8_t nTG);
	void setPitchbendRange (uint8_t range, uint8_t nTG);
	void setPitchbendStep (uint8_t step, uint8_t nTG);
	void setPortamentoMode (uint8_t mode, uint8_t nTG);
	void setPortamentoGlissando (uint8_t glissando, uint8_t nTG);
	void setPortamentoTime (uint8_t time, uint8_t nTG);
	void setModWheelRange (uint8_t range, uint8_t nTG);
	void setModWheelTarget (uint8_t target, uint8_t nTG);
	void setFootControllerRange (uint8_t range, uint8_t nTG);
	void setFootControllerTarget (uint8_t target, uint8_t nTG);
	void setBreathControllerRange (uint8_t range, uint8_t nTG);
	void setBreathControllerTarget (uint8_t target, uint8_t nTG);
	void setAftertouchRange (uint8_t range, uint8_t nTG);
	void setAftertouchTarget (uint8_t target, uint8_t nTG);
	void loadVoiceParameters (const uint8_t *data, uint8_t nTG);
	void setVoiceDataElement (uint8_t data, uint8_t number, uint8_t nTG);
	void getSysExVoiceDump (uint8_t *dest, uint8_t nTG);
	void setOPMask (uint8_t uchOPMask, uint8_t nTG);
	int16_t checkSystemExclusive (const uint8_t *pMessage, const uint16_t nLength, uint8_t nTG);
	unsigned GetPerformanceSelectChannel (void);
	void setMasterVolume (float32_t vol);

private:
	CMIDIDevice *m_pDevice = 0;
};

class CUserInterface
{
public:
	void UIMIDICmdHandler (unsigned nMidiCh, unsigned nMidiType, unsigned nMidiData1, unsigned nMidiData2);
};

// Stand-in for the CConfig getters src/mididevice.cpp reads (src/config.cpp includes
// Synth_Dexed's dexed.h, so it is not compiled; midi_standin.cc defines the getters).
struct TStandinConfig
{
	unsigned nToneGenerators;
	bool bMIDIDump, bRXProgramChange, bThruIgnoreClock, bThruIgnoreActiveSensing, bIgnoreAllNotesOff;
	std::string ThruIn, ThruOut, Thru2In, Thru2Out;
	unsigned nCCVol, nCCPan, nCCDetune, nGlobalExpression;
};
extern TStandinConfig g_StandinConfig;
#endif
