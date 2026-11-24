// Known-answer tests for the FTP server's control channel (src/net/ftpworker.cpp), built with
// normal flags and run by mayhem/test.sh, over the stand-ins in mayhem/harnesses/ftp_standin.cc.
#include <stdio.h>
#include <string.h>
#include <string>
#include "ftp_standin.h"

static int g_nPassed, g_nFailed;
static void Check (bool b, const char *pName)
{
	printf ("%s %s\n", b ? "PASS" : "FAIL", pName);
	if (!b)
		printf ("  control: %s\n  data: %s\n", g_FtpControlOut.c_str (), g_FtpDataOut.c_str ());
	(b ? g_nPassed : g_nFailed)++;
}
static void Session (const std::string &s) { FtpSession ((const uint8_t *) s.data (), s.size ()); }
static bool Out (const char *p) { return g_FtpControlOut.find (p) != std::string::npos; }

int main (void)
{
	const std::string Login = "USER admin\r\nPASS minidexed\r\n";

	Session ("");
	Check (g_FtpControlOut.compare (0, 4, "220 ") == 0, "ftp: greeting is 220");

	Session ("USER admin\r\nPASS wrong\r\n");
	Check (Out ("331 ") && Out ("\r\n530 "), "ftp: wrong password -> 530");

	// (A command that checks the login before USER and PASS were both sent compares an unset
	// Circle CString - strcmp (NULL, ...) - so the oracle does not send one.)
	Session ("PASS minidexed\r\n");
	Check (Out ("\r\n332 "), "ftp: PASS before USER -> 332 need account");

	Session (Login + "PWD\r\n");
	Check (Out ("230 ") && Out ("257 \"/\""), "ftp: login then PWD is /");

	Session (Login + "CWD /SD/performance\r\nPWD\r\n");
	Check (Out ("257 \"/SD/performance\""), "ftp: CWD into a subdirectory");

	Session (Login + "CWD /SD/nosuchdir\r\nPWD\r\n");
	Check (Out ("550 ") && Out ("257 \"/\""), "ftp: CWD to a missing dir fails and stays");

	Session (Login + "CWD /SD/sysex/voice\r\nCDUP\r\nPWD\r\n");
	Check (Out ("257 \"/SD/sysex\""), "ftp: CDUP goes to the parent");

	Session (Login + "PORT 10,0,0,1,4,1\r\n");
	Check (Out ("200 "), "ftp: PORT h1,h2,h3,h4,p1,p2 accepted");

	Session (Login + "PORT 10,0,0\r\n");
	Check (Out ("501 "), "ftp: short PORT -> 501 syntax error");

	Session (Login + "PASV\r\n");
	Check (Out ("227 ") && Out ("(10,0,0,10,35,40)"), "ftp: PASV reports 10.0.0.10 port 9000");

	Session (Login + "CWD /SD\r\nPORT 10,0,0,1,4,1\r\nNLST\r\n");
	Check (g_FtpDataOut.find ("minidexed.ini\r\n") != std::string::npos
	       && g_FtpDataOut.find ("performance.ini\r\n") != std::string::npos
	       && g_FtpDataOut.find ("sysex") == std::string::npos, "ftp: NLST lists the files (not dirs) of the SD root");

	Session (Login + "CWD /SD\r\nPORT 10,0,0,1,4,1\r\nRETR minidexed.ini\r\n");
	Check (g_FtpDataOut == "ToneGenerators=8\r\n" && Out ("226 "), "ftp: RETR sends the file");

	std::string Data;
	Session (Login + "CWD /SD\r\nPORT 10,0,0,1,4,1\r\nSTOR new.ini\r\n");
	Check (FtpFile ("SD:/new.ini", &Data) && Data == "Volume1=99\r\n", "ftp: STOR writes the uploaded data");

	Session (Login + "CWD /SD\r\nDELE minidexed.ini\r\n");
	Check (!FtpFile ("SD:/minidexed.ini", nullptr) && Out ("250 "), "ftp: DELE removes the file");

	Session (Login + "CWD /SD\r\nMKD banks\r\n");
	Check (FtpFile ("SD:/banks", nullptr) && Out ("257 "), "ftp: MKD creates a directory");

	Session (Login + "CWD /SD\r\nRNFR performance.ini\r\nRNTO perf2.ini\r\n");
	Check (FtpFile ("SD:/perf2.ini", nullptr) && !FtpFile ("SD:/performance.ini", nullptr) && Out ("350 "),
	       "ftp: RNFR/RNTO renames");

	Session (Login + "BOGUS\r\n");
	Check (Out ("502 "), "ftp: unknown command -> 502");

	Session (Login + "QUIT\r\nPWD\r\n");
	Check (Out ("221 ") && !Out ("257 "), "ftp: QUIT closes the session (reboot)");

	printf ("RESULT passed=%d failed=%d\n", g_nPassed, g_nFailed);
	return g_nFailed ? 1 : 0;
}
