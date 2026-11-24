// Known-answer functional tests for MiniDexed's host-buildable input paths, built with normal
// flags by mayhem/build.sh and run by mayhem/test.sh:
//   - CSysExFileLoader (src/sysexfileloader.cpp): DX7 bank file loading, naming, voice decoding
//   - CAppleMIDIParticipant (src/net/applemidi.cpp): session setup, RTP-MIDI decoding, teardown
// Prints "PASS <name>" / "FAIL <name>" per check and a final "RESULT passed=<p> failed=<f>".
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <deque>
#include <string>
#include <vector>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include "sysexfileloader.h"
#include "net/applemidi.h"
#include <fatfs/ff.h>
#include "performanceconfig.h"
#include "mididevice.h"

static int g_nPassed, g_nFailed;
static void Check (bool bOK, const char *pName)
{
	printf ("%s %s\n", bOK ? "PASS" : "FAIL", pName);
	(bOK ? g_nPassed : g_nFailed)++;
}

// ---------------------------------------------------------------- sysex
static void WriteFile (const std::string &Path, const std::vector<uint8_t> &Data)
{
	FILE *f = fopen (Path.c_str (), "wb");
	if (!f) { perror (Path.c_str ()); exit (2); }
	fwrite (Data.data (), 1, Data.size (), f);
	fclose (f);
}

static std::vector<uint8_t> MakeVoices (const char *pName0)
{
	std::vector<uint8_t> v (4096, 0);
	uint8_t *p = v.data ();		// voice 0
	p[0] = 120;			// OP6 EG rate 1, out of range -> clamped to 99
	p[1] = 42;
	p[12] = 0x3A;			// detune 7, rate scaling 2
	p[15] = 0x05;			// coarse 2, mode 1
	p[110] = 21;			// algorithm 22 (0-based 21)
	p[111] = 0x0D;			// osc key sync 1, feedback 5
	p[116] = 0x35;			// pms 3, waveform 2, lfo sync 1
	p[117] = 24;			// transpose
	memcpy (&p[118], pName0, 10);
	for (unsigned i = 1; i < 32; i++)
		memcpy (&v[i * 128 + 118], "OTHERVOICE", 10);
	return v;
}

static std::vector<uint8_t> MakeBank (const char *pName0)
{
	std::vector<uint8_t> b = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
	std::vector<uint8_t> v = MakeVoices (pName0);
	b.insert (b.end (), v.begin (), v.end ());
	b.push_back (0x00);
	b.push_back (0xF7);
	return b;
}

static void TestSysEx (const std::string &Root)
{
	std::string Sysex = Root + "/sysex", Voice = Sysex + "/voice";
	mkdir (Sysex.c_str (), 0700);
	mkdir (Voice.c_str (), 0700);
	mkdir ((Voice + "/sub").c_str (), 0700);
	WriteFile (Voice + "/000001_mybank.syx", MakeBank ("TESTVOICE1"));
	WriteFile (Voice + "/sub/000004_deep.syx", MakeBank ("DEEPVOICE4"));
	std::vector<uint8_t> Bad = MakeBank ("BADVOICE00");
	Bad[1] = 0x41;			// not Yamaha
	WriteFile (Voice + "/000002_bad.syx", Bad);
	WriteFile (Voice + "/000003_raw.syx", MakeVoices ("RAWVOICE03"));
	WriteFile (Voice + "/readme.txt", {'h', 'i'});

	{
		CSysExFileLoader L (Sysex.c_str ());
		L.Load (false);
		Check (L.IsValidBank (0), "sysex: bank 1 with header loads");
		Check (!L.IsValidBank (1), "sysex: bank with wrong manufacturer ID rejected");
		Check (!L.IsValidBank (2), "sysex: headerless bank rejected when option off");
		Check (L.IsValidBank (3), "sysex: bank in subdirectory loads");
		Check (L.GetNumHighestBank () == 3, "sysex: highest bank index is 3");
		Check (L.GetBankName (0) == "mybank", "sysex: bank name parsed from file name");
		Check (L.GetBankName (1) == "NO NAME", "sysex: unloaded bank has no name");
		Check (L.GetVoiceName (0, 0) == "TESTVOICE1", "sysex: voice name of bank 1 voice 1");
		Check (L.GetVoiceName (3, 0) == "DEEPVOICE4", "sysex: voice name in subdirectory bank");
		Check (L.GetVoiceName (0, 5) == "OTHERVOICE", "sysex: voice name of bank 1 voice 6");
		Check (L.GetNextBankUp (0) == 3, "sysex: next bank up skips missing banks");
		Check (L.GetNextBankDown (0) == 3, "sysex: next bank down wraps around");

		uint8_t d[156];
		L.GetVoice (0, 0, d);
		Check (d[0] == 99 && d[1] == 42, "sysex: decoded EG rates (with clamp)");
		Check (d[13] == 2 && d[20] == 7, "sysex: decoded rate scaling / detune");
		Check (d[17] == 1 && d[18] == 2, "sysex: decoded osc mode / coarse");
		Check (d[134] == 21 && d[135] == 5 && d[136] == 1, "sysex: decoded algorithm / feedback / key sync");
		Check (d[141] == 1 && d[142] == 2 && d[143] == 3, "sysex: decoded lfo sync / wave / pms");
		Check (d[144] == 24 && memcmp (&d[145], "TESTVOICE1", 10) == 0 && d[155] == 0x3f,
		       "sysex: decoded transpose / name / op enable");
		L.GetVoice (1, 0, d);
		Check (memcmp (&d[145], "INIT VOICE", 10) == 0, "sysex: missing bank yields INIT VOICE");
	}
	{
		CSysExFileLoader L (Sysex.c_str ());
		L.Load (true);
		Check (L.IsValidBank (2), "sysex: headerless bank loads when option on");
		Check (L.GetVoiceName (2, 0) == "RAWVOICE03", "sysex: headerless bank voice name");
	}
}

// ---------------------------------------------------------------- AppleMIDI
u64 g_ShimClockTicks = 1000000;

struct TDatagram { bool bMIDI; u32 nIP; u16 nPort; std::vector<u8> Data; };
static std::deque<TDatagram> g_In;
static bool g_bHave;
static TDatagram g_Cur;
static std::vector<std::vector<u8>> g_Sent, g_MIDI;
static std::string g_Connected;
static int g_nDisconnects;

static void Next (void)
{
	g_bHave = !g_In.empty ();
	if (g_bHave) { g_Cur = g_In.front (); g_In.pop_front (); }
	g_ShimClockTicks += 1000;
}

int ShimSocketReceiveFrom (u16 nOwnPort, void *pBuffer, unsigned nLength, CIPAddress *pIP, u16 *pPort)
{
	if (!g_bHave || g_Cur.bMIDI != (nOwnPort == 5005))
		return 0;
	unsigned n = g_Cur.Data.size () < nLength ? g_Cur.Data.size () : nLength;
	memcpy (pBuffer, g_Cur.Data.data (), n);
	pIP->Set (g_Cur.nIP);
	*pPort = g_Cur.nPort;
	return n;
}

int ShimSocketSendTo (u16, const void *pBuffer, unsigned nLength, const CIPAddress &, u16)
{
	g_Sent.emplace_back ((const u8 *) pBuffer, (const u8 *) pBuffer + nLength);
	return nLength;
}

void ShimSchedulerYield (void)
{
	Next ();
	if (!g_bHave)
		throw CShimInputDone ();
}

class CHandler : public CAppleMIDIHandler
{
	void OnAppleMIDIDataReceived (const u8 *p, size_t n) override { g_MIDI.emplace_back (p, p + n); }
	void OnAppleMIDIConnect (const CIPAddress *, const char *pName) override { g_Connected = pName; }
	void OnAppleMIDIDisconnect (const CIPAddress *, const char *pName) override { (void) pName; g_nDisconnects++; }
};

static std::vector<u8> Session (const char Cmd[2], const char *pName)
{
	std::vector<u8> v = {0xFF, 0xFF, (u8) Cmd[0], (u8) Cmd[1], 0, 0, 0, 2,
			     0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD};
	if (pName)
		v.insert (v.end (), pName, pName + strlen (pName) + 1);
	return v;
}

static std::vector<u8> RTP (std::vector<u8> Section)
{
	std::vector<u8> v = {0x80, 0x61, 0x00, 0x01, 0, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD};
	v.insert (v.end (), Section.begin (), Section.end ());
	return v;
}

static void TestAppleMIDI (void)
{
	const u32 IP = 0x0100000A, Other = 0x0200000A;
	g_In.push_back ({false, IP, 6000, Session ("IN", "hostsess")});
	g_In.push_back ({true, Other, 6001, Session ("IN", "intruder")});	// wrong peer: rejected
	g_In.push_back ({true, IP, 6001, Session ("IN", "hostsess")});
	g_In.push_back ({true, IP, 6001, RTP ({0x03, 0x90, 0x3C, 0x7F})});
	g_In.push_back ({true, IP, 6001, RTP ({0x06, 0x80, 0x3C, 0x40, 0x00, 0x3E, 0x7F})});
	g_In.push_back ({true, IP, 6001, RTP ({0x04, 0xF0, 0x43, 0x10, 0xF7})});
	g_In.push_back ({true, IP, 6001, RTP ({0x80, 0x02, 0xC0, 0x05})});	// B flag: 12-bit length
	g_In.push_back ({true, IP, 9999, RTP ({0x02, 0xC0, 0x07})});		// wrong port: ignored
	g_In.push_back ({false, IP, 6000, Session ("BY", nullptr)});

	CBcmRandomNumberGenerator Random;
	CHandler Handler;
	CAppleMIDIParticipant P (&Random, &Handler, "MiniDexed");
	Check (P.Initialize (), "applemidi: participant initializes");
	Next ();
	try { P.Run (); } catch (const CShimInputDone &) {}

	Check (g_Sent.size () >= 3, "applemidi: replies sent");
	Check (g_Sent.size () >= 1 && g_Sent[0].size () > 16 && g_Sent[0][2] == 'O' && g_Sent[0][3] == 'K'
	       && memcmp (&g_Sent[0][8], "\x11\x22\x33\x44", 4) == 0
	       && strcmp ((const char *) &g_Sent[0][16], "MiniDexed") == 0,
	       "applemidi: control invitation accepted with token and session name");
	Check (g_Sent.size () >= 2 && g_Sent[1][2] == 'N' && g_Sent[1][3] == 'O',
	       "applemidi: invitation from unexpected peer rejected");
	Check (g_Sent.size () >= 3 && g_Sent[2][2] == 'O' && g_Sent[2][3] == 'K',
	       "applemidi: MIDI invitation accepted");
	Check (g_Connected == "hostsess", "applemidi: connect callback carries session name");
	std::vector<std::vector<u8>> Want = {
		{0x90, 0x3C, 0x7F}, {0x80, 0x3C, 0x40}, {0x3E, 0x7F}, {0xF0, 0x43, 0x10, 0xF7}, {0xC0, 0x05}};
	Check (g_MIDI == Want, "applemidi: MIDI commands decoded (running status, delta time, SysEx, long header)");
	Check (g_nDisconnects == 1, "applemidi: end session from initiator disconnects");
	Check (g_Sent.size () >= 3 && g_MIDI.size () == 5, "applemidi: packet from wrong port ignored");
}

// ---------------------------------------------------------------- performance.ini
// In-memory FatFs stand-in: f_read serves g_PerfFile, f_write appends to g_PerfSaved.
static std::string g_PerfFile, g_PerfSaved;
static size_t g_nPerfPos;
extern "C" {
FRESULT f_open (FIL *, const TCHAR *, BYTE nMode)
{
	if (nMode & FA_READ) g_nPerfPos = 0; else g_PerfSaved.clear ();
	return FR_OK;
}
FRESULT f_read (FIL *, void *pBuffer, UINT nBytes, UINT *pRead)
{
	size_t n = g_PerfFile.size () - g_nPerfPos < nBytes ? g_PerfFile.size () - g_nPerfPos : nBytes;
	memcpy (pBuffer, g_PerfFile.data () + g_nPerfPos, n);
	g_nPerfPos += n;
	*pRead = n;
	return FR_OK;
}
FRESULT f_write (FIL *, const void *pBuffer, UINT nBytes, UINT *pWritten)
{
	g_PerfSaved.append ((const char *) pBuffer, nBytes);
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

static void TestPerformance (void)
{
	std::string Voice;
	for (unsigned i = 0; i < NUM_VOICE_PARAM; i++)
	{
		char b[4];
		snprintf (b, sizeof b, "%02X ", i == 0 ? 0x5F : i == 1 ? 0x1d : (i * 7) & 0x7F);
		Voice += b;
	}
	g_PerfFile = "# test performance\n"
		"BankNumber1=5\nVoiceNumber1=3\nMIDIChannel1=2\nMIDIChannel2=17\n"
		"Volume1=90\nPan1=10\nDetune1=-7\nNoteShift1=12\n"
		"MonoMode1=1\nPortamentoTime1=42\n"
		"VoiceData1=" + Voice + "\n"
		"CompressorEnable=0\nReverbSize=33\n";
	FATFS Fs;
	CPerformanceConfig P (&Fs);
	Check (P.Load (), "performance: file with enabled TG loads");
	Check (P.GetBankNumber (0) == 5 && P.GetVoiceNumber (0) == 2, "performance: bank / 1-based voice number");
	Check (P.GetMIDIChannel (0) == 1, "performance: MIDI channel 2 -> index 1");
	Check (P.GetMIDIChannel (1) == CMIDIDevice::OmniMode, "performance: MIDI channel >16 -> omni");
	Check (P.GetMIDIChannel (2) == CMIDIDevice::Disabled, "performance: missing MIDI channel -> disabled");
	Check (P.GetVolume (0) == 90 && P.GetPan (0) == 10, "performance: volume / pan");
	Check (P.GetDetune (0) == -7 && P.GetNoteShift (0) == 12, "performance: signed detune / note shift");
	Check (P.GetVolume (2) == 100 && P.GetPan (2) == 64 && P.GetCutoff (2) == 99, "performance: defaults for absent keys");
	Check (P.GetMonoMode (0) && P.GetPortamentoTime (0) == 42, "performance: mono mode / portamento");
	Check (!P.GetCompressorEnable () && P.GetReverbSize () == 33 && P.GetReverbEnable (), "performance: effect settings");
	Check (P.VoiceDataFilled (0) && !P.VoiceDataFilled (1), "performance: voice data presence");
	const uint8_t *pV = P.GetVoiceDataFromTxt (0);
	Check (pV[0] == 0x5F && pV[1] == 0x1D && pV[2] == 14 && pV[155] == ((155 * 7) & 0x7F), "performance: hex voice data decoded");
	P.Init (CConfig::AllToneGenerators);
	P.Save ();
	Check (g_PerfSaved.find ("Volume1=90") != std::string::npos
	       && g_PerfSaved.find ("Detune1=-7") != std::string::npos
	       && g_PerfSaved.find ("MIDIChannel2=255\n") != std::string::npos
	       && g_PerfSaved.find ("MIDIChannel3=0\n") != std::string::npos,
	       "performance: saved file round-trips values");
	g_PerfFile = "Volume1=90\n";
	CPerformanceConfig Q (&Fs);
	Check (!Q.Load (), "performance: file without any MIDI channel reports no enabled TG");
}

int main (void)
{
	const char *pTmp = getenv ("TMPDIR");
	std::string Templ = std::string (pTmp && *pTmp ? pTmp : "/tmp") + "/minidexed-test-XXXXXX";
	if (!mkdtemp (&Templ[0])) { perror ("mkdtemp"); return 2; }
	TestSysEx (Templ);
	TestAppleMIDI ();
	TestPerformance ();
	std::string Cmd = "rm -rf '" + Templ + "'";
	if (system (Cmd.c_str ())) {}
	printf ("RESULT passed=%d failed=%d\n", g_nPassed, g_nFailed);
	return g_nFailed ? 1 : 0;
}
