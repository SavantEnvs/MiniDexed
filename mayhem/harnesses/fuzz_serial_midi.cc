// libFuzzer harness: serial MIDI input (src/serialmididevice.cpp -> src/mididevice.cpp).
//
// The bytes arriving on the MIDI UART go through the real CSerialMIDIDevice::Process () (running
// status, real-time bytes, SysEx assembly up to MAX_MIDI_MESSAGE) into the real
// CMIDIDevice::MIDIMessageHandler () (MIDI Thru, global SysEx, performance select, per-TG
// note/CC/RPN/SysEx dispatch, HandleSystemExclusive / SendSystemExclusiveVoice). USB and
// UDP/RTP-MIDI devices feed the same MIDIMessageHandler (). CMiniDexed, CUserInterface and the
// CConfig getters are recording stand-ins (mayhem/shim/midi/standin.h, midi_standin.cc).
//
// Input: [cfg:8][serial bytes...]
//   cfg[0] bit0 16 TGs (else 8)       bit1 MIDIDumpEnabled         bit2 MIDIRXProgramChange
//          bit3 MIDIThruIgnoreClock   bit4 ...IgnoreActiveSensing  bit5 MIDIThruIn = ttyS1
//          bit6 MIDIThruOut = ttyS1   bit7 MIDIThru2In/Out = ttyS1
//   cfg[1..3] MIDISystemCCVol/Pan/Detune (mod 8, the size of upstream's MIDISystemCCMap)
//   cfg[4] MIDIGlobalExpression (mod 18)   cfg[5] performance select channel (mod 18)
//   cfg[6] channel of TG0 (mod 18: 0-15, 16 = omni, 17 = disabled); TG n gets
//          (cfg[6] + n * (cfg[7] >> 5)) mod 18
//   cfg[7] bit0..3 SysEx reject code (-1..-11 when not accepted), bit4 SysEx accepted
// These are the configuration values upstream's config.cpp/performance file can set (it
// validates the TG count; the CC map indexes are trusted as-is there and kept in range here).
// The serial bytes are handed out by the shim UART, at most 100 per Process () call as upstream
// reads them, until all are consumed.
// Per-input state: the device is constructed per input at a fixed static address (upstream's
// static device map keeps the pointer AddDevice () registered, and a std::map insert never
// replaces it), destroyed after the input; the stand-in state is reset per input.
#include <stdint.h>
#include <stddef.h>
#include <new>
#include "serialmididevice.h"

namespace {
alignas (CSerialMIDIDevice) unsigned char g_DeviceStorage[sizeof (CSerialMIDIDevice)];
CMiniDexed g_Synth;
CUserInterface g_UI;
CInterruptSystem g_Interrupt;
volatile size_t g_Sink;
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *pData, size_t nSize)
{
	if (nSize < 8)
		return -1;
	const uint8_t *c = pData;
	TStandinConfig &C = g_StandinConfig;
	C.nToneGenerators = (c[0] & 1) ? CConfig::AllToneGenerators : CConfig::MinToneGenerators;
	C.bMIDIDump = c[0] & 2;
	C.bRXProgramChange = c[0] & 4;
	C.bThruIgnoreClock = c[0] & 8;
	C.bThruIgnoreActiveSensing = c[0] & 0x10;
	C.bIgnoreAllNotesOff = false;
	C.ThruIn = (c[0] & 0x20) ? "ttyS1" : "";
	C.ThruOut = (c[0] & 0x40) ? "ttyS1" : "";
	C.Thru2In = C.Thru2Out = (c[0] & 0x80) ? "ttyS1" : "";
	C.nCCVol = c[1] % 8;
	C.nCCPan = c[2] % 8;
	C.nCCDetune = c[3] % 8;
	C.nGlobalExpression = c[4] % 18;
	g_StandinPerformanceSelectChannel = c[5] % 18;
	g_StandinSysExAccept = c[7] & 0x10;
	g_StandinSysExReject = -1 - (int16_t) ((c[7] & 0x0F) % 11);
	g_StandinCalls.clear ();
	g_ShimSerialTx.clear ();

	alignas (CConfig) static unsigned char ConfigStorage[sizeof (CConfig)];
	CConfig *pConfig = new (ConfigStorage) CConfig (nullptr);	// getters read g_StandinConfig

	CSerialMIDIDevice *pDevice = new (g_DeviceStorage) CSerialMIDIDevice (&g_Synth, &g_Interrupt, pConfig, &g_UI);
	pDevice->Initialize ();
	g_Synth.RegisterMIDIDevice (pDevice);
	for (unsigned nTG = 0; nTG < C.nToneGenerators; nTG++)
		g_Synth.SetMIDIChannel ((uint8_t) ((c[6] + nTG * (c[7] >> 5)) % 18), nTG);

	g_ShimSerialRx.pData = pData + 8;
	g_ShimSerialRx.nSize = nSize - 8;
	g_ShimSerialRx.nPos = 0;
	while (g_ShimSerialRx.nPos < g_ShimSerialRx.nSize)
		pDevice->Process ();

	g_Sink += g_StandinCalls.size () + g_ShimSerialTx.size ();
	g_Synth.RegisterMIDIDevice (nullptr);
	pDevice->~CSerialMIDIDevice ();
	pConfig->~CConfig ();
	return 0;
}
