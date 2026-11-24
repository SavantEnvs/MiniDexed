// Known-answer tests for the serial MIDI input path (src/serialmididevice.cpp ->
// src/mididevice.cpp), built with normal flags and run by mayhem/test.sh. The recording
// stand-ins (mayhem/harnesses/midi_standin.cc) log what the parser dispatched to the synth.
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <new>
#include <string>
#include <vector>
#include "serialmididevice.h"

static int g_nPassed, g_nFailed;
static void Check (bool b, const char *pName)
{
	printf ("%s %s\n", b ? "PASS" : "FAIL", pName);
	(b ? g_nPassed : g_nFailed)++;
}

static bool Has (const char *pCall)
{
	return std::find (g_StandinCalls.begin (), g_StandinCalls.end (), pCall) != g_StandinCalls.end ();
}
static size_t Count (const char *pPrefix)
{
	size_t n = 0;
	for (const std::string &s : g_StandinCalls)
		n += s.compare (0, strlen (pPrefix), pPrefix) == 0;
	return n;
}

// TG n listens on channel Channels[n]; 8 TGs unless given more.
static void Run (const std::vector<uint8_t> &Bytes, std::vector<uint8_t> Channels = {0, 1, 2, 3, 4, 5, 6, 7})
{
	static CMiniDexed Synth;
	static CUserInterface UI;
	static CInterruptSystem Intr;
	static CConfig Config (nullptr);
	alignas (CSerialMIDIDevice) static unsigned char Storage[sizeof (CSerialMIDIDevice)];
	g_StandinConfig.nToneGenerators = Channels.size ();
	g_StandinCalls.clear ();
	g_ShimSerialTx.clear ();
	CSerialMIDIDevice *p = new (Storage) CSerialMIDIDevice (&Synth, &Intr, &Config, &UI);
	Synth.RegisterMIDIDevice (p);
	for (unsigned i = 0; i < Channels.size (); i++)
		Synth.SetMIDIChannel (Channels[i], i);
	g_StandinCalls.clear ();
	g_ShimSerialRx.pData = Bytes.data ();
	g_ShimSerialRx.nSize = Bytes.size ();
	g_ShimSerialRx.nPos = 0;
	while (g_ShimSerialRx.nPos < g_ShimSerialRx.nSize)
		p->Process ();
	Synth.RegisterMIDIDevice (nullptr);
	p->~CSerialMIDIDevice ();
}

static void Defaults (void)
{
	g_StandinConfig = TStandinConfig {8, false, true, false, false, false, "", "", "", "", 0, 0, 0, 0};
	g_StandinPerformanceSelectChannel = CMIDIDevice::Disabled;
	g_StandinSysExAccept = false;
	g_StandinSysExReject = -1;
}

int main (void)
{
	Defaults ();
	Run ({0x90, 0x3C, 0x64});
	Check (Has ("keydown(60,100,0)"), "midi: note on ch1 -> keydown on TG0");
	Check (Has ("UI(0,9,60,100)"), "midi: note on is offered to the UI handler");
	Check (Count ("keydown") == 1, "midi: note on reaches only the TG on that channel");

	Run ({0x91, 0x40, 0x50, 0x41, 0x51});
	Check (Has ("keydown(64,80,1)") && Has ("keydown(65,81,1)"), "midi: running status repeats note on");

	Run ({0x90, 0x3C, 0x00, 0x80, 0x3D, 0x10});
	Check (Has ("keyup(60,0)") && Has ("keyup(61,0)"), "midi: velocity-0 note on and note off -> keyup");

	Run ({0x90, 0x3C, 0xF8, 0x64});
	Check (Has ("keydown(60,100,0)"), "midi: real-time byte inside a message does not break it");

	Run ({0xB2, 0x07, 0x65, 0xB2, 0x0A, 0x20, 0xB2, 0x40, 0x7F});
	Check (Has ("SetVolume(101,2)"), "midi: CC7 -> volume on TG2");
	Check (Has ("SetPan(32,2)"), "midi: CC10 -> pan on TG2");
	Check (Has ("setSustain(1,2)"), "midi: CC64 >= 64 -> sustain on");

	Run ({0xE0, 0x00, 0x40});
	Check (Count ("setPitchbend(0,0)") == 1, "midi: centered pitch bend is 0");

	Run ({0xD3, 0x55});
	Check (Has ("setAftertouch(85,3)"), "midi: 2-byte channel aftertouch");

	Run ({0x90, 0x3C, 0x64}, {16, 16, 17, 17, 17, 17, 17, 17});
	Check (Count ("keydown(60,100,") == 2, "midi: omni TGs both play, disabled ones do not");

	// TX816 "set MIDI channel" global SysEx: TG3 -> channel 6
	Run ({0xF0, 0x43, 0x03, 0x04, 0x01, 0x05, 0xF7});
	Check (Has ("SetMIDIChannel(5,3)"), "midi: TX816 SysEx sets TG MIDI channel");

	// Universal master volume: 14-bit 0x2000 / 16384 = 0.5
	Run ({0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x40, 0xF7});
	Check (Has ("setMasterVolume(0.5000)"), "midi: universal SysEx master volume");

	// Voice dump request F0 43 20 00 F7 on channel 0 -> 163-byte dump back out of the UART
	Run ({0xF0, 0x43, 0x20, 0x00, 0xF7});
	Check (Has ("getSysExVoiceDump(0)"), "midi: dump request reads the TG voice");
	Check (g_ShimSerialTx.size () == 163 && g_ShimSerialTx[0] == 0xF0 && g_ShimSerialTx[162] == 0xF7,
	       "midi: dump request sends a 163-byte voice dump");

	// Parameter change accepted by the SysEx checker -> setVoiceDataElement
	g_StandinSysExAccept = true;
	Run ({0xF0, 0x43, 0x10, 0x01, 0x06, 0x2A, 0xF7});
	Check (Has ("setVoiceDataElement(134,42,0)") && Has ("notesOff(0,0)"),
	       "midi: SysEx parameter 134 (algorithm) change sets voice data and stops notes");

	// Single-voice dump accepted -> loadVoiceParameters with the 155 payload bytes
	std::vector<uint8_t> Dump = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
	for (unsigned i = 0; i < 155; i++)
		Dump.push_back (i == 0 ? 0x02 : 0x00);
	Dump.push_back (0x00);
	Dump.push_back (0xF7);
	Run (Dump);
	Check (Has ("checkSystemExclusive(163,0)"), "midi: 163-byte SysEx assembled from the serial stream");
	Check (Has ("loadVoiceParameters(2,0)"), "midi: accepted voice dump loads the voice");

	// Rejected SysEx: nothing loaded
	g_StandinSysExAccept = false;
	Run (Dump);
	Check (Count ("loadVoiceParameters") == 0, "midi: rejected voice dump is not loaded");

	// MIDI Thru from ttyS1 to itself, ignoring clock
	Defaults ();
	g_StandinConfig.ThruIn = g_StandinConfig.ThruOut = "ttyS1";
	g_StandinConfig.bThruIgnoreClock = true;
	Run ({0xF8, 0x90, 0x3C, 0x64});
	Check (g_ShimSerialTx == std::vector<uint8_t> ({0x90, 0x3C, 0x64}), "midi: thru forwards the note, not the clock");

	// Performance select channel: program change selects a performance
	Defaults ();
	g_StandinPerformanceSelectChannel = 9;
	Run ({0xC9, 0x05});
	Check (Has ("ProgramChangePerformance(5)"), "midi: PC on performance channel selects performance");

	printf ("RESULT passed=%d failed=%d\n", g_nPassed, g_nFailed);
	return g_nFailed ? 1 : 0;
}
