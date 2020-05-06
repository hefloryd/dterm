/* dterm.c	Dumb terminal program				19/11/2004/dcs
 */

/*
 * Copyright 2007 Knossos Networks Ltd
 * Copyright 2017 Don Stokes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef VERSION
#define VERSION "unknown"
#endif
#define COPYRIGHT \
"dterm version " VERSION " Copyright 2007 Don Stokes\n" \
"\n" \
"This program is free software; you can redistribute it and/or\n" \
"modify it under the terms of the GNU General Public License version 2\n" \
"as published by the Free Software Foundation.\n" \
"\n" \
"This program is distributed in the hope that it will be useful,\n" \
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n" \
"GNU General Public License for more details.\n" \
"\n" \
"You should have received a copy of the GNU General Public License\n" \
"along with this program; if not, write to the Free Software\n" \
"Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA\n" \
"02110-1301, USA.\n"


#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif /* READLINE */


#define FIXTTY	if(istty) tcsetattr(0, TCSADRAIN, &savetio)
#define DIEP(s)  { FIXTTY; perror(s); exit(1); }
#define DIE(s,m) { FIXTTY; fprintf(stderr, "%s: %s\n", s, m); exit(1); }

/*
 Sigh
 */
#ifndef CCTS_OFLOW
#ifdef CRTSCTS
#define CCTS_OFLOW CRTSCTS
#endif /* CRTSCTS */
#endif /* ! CCTS_OFLOW */


/*
 Valid speed settings. 
 */
#include "speeds.h"

/*
 Save the terminal settings here
 */
static struct termios intio, savetio;

static int fd = -1;		/* Channel to port			*/
static int cmdchar = 035;	/* Command character, default ^]	*/
static char *device = 0;	/* Device, no default			*/
static int ispeed = B9600;	/* Input speed, default 9600 bps	*/
static int ospeed = B9600;	/* Output speed, default 9600 bps	*/
static int twostop = 0;		/* Stop bits, if set to CSTOPB, use two	*/
static int parity = 0;		/* Parity, -1 = mark, -2 = space 0=none */
				/* PARENB enables parity, PARODD = odd	*/
static int bits = CS8;		/* CS5-CS8 for 5-8 bits per character	*/
static int ctsflow = 0;		/* CCTS_OFLOW enables CTS flow control	*/
static int xonflow = 0;		/* IXON | IXOFF for in & out soft F/c	*/
static int modemcontrol = 0;	/* Set to HUPCLto enable modem control	*/
static int markparity = 0;	/* Mark parity: set high bit on output	*/
static int spaceparity = 0;	/* Space parity: clear high output bit	*/
static int backspace = 0;	/* Backspace char, send on BS or DEL 	*/
static int maplf = 0;		/* If set, map LF to CR 		*/
static int ignorecr = 0;	/* Ignore carriage returns		*/
static int crlf = 0;		/* Send CRLF on CR			*/
static int istty;		/* Result of isatty()			*/
static char *connname = 0;	/* Connection name found in config	*/
static int sendbreak = 0;	/* Break requested			*/
static int modem = 0;		/* Current modem status			*/
static int setmodem = 0;	/* Set modem status to this		*/
static int delay = 0;		/* Millisecond delay after each key	*/
static int linedelay = 0;	/* Millisecond delay after each CR	*/
static int showspecial = 0;	/* Show special chars as [xx]		*/
static int noshell = 0;		/* Prevent !<command> execution		*/

/* Hayyyeeellllllppppp!!!!
 */
static void
help() {
	fprintf(stderr, 
"dterm commands:\n"
"^%c		Enter command mode	quit, q		Exit\n"
"show		Display configuration	help, h, ?	Display this message\n"
"!<command>	Execute shell command	version		Display version\n"
"@<filename>	Get configuration from <filename> or !<command>\n"
"<device>	Connect to <device>	300,1200,9600	Set speed\n"
"5, 6, 7, 8	Set bits per character	1, 2		Set stop bits\n"
"e, o, n, m, s	Set parity		speeds		Show available speeds\n"
"xon,noxon	XON/XOFF flow control"
#ifdef CCTS_OFLOW
"\tcts,nocts	CTS flow control\n"
#else
"\n"
#endif
"dtr, nodtr	Raise / lower DTR	b		Send a 500ms break\n"
"rts, norts	Raise / lower RTS	d, r		Toggle DTR, RTS\n"
"rx <filename>	Receive file (XMODEM)	sx <filename>	Send file (XMODEM)\n"
"rz		Receive file (ZMODEM)	sz <filename>	Send file (ZMODEM)\n"
"modem		Hang up modem on exit, exit if modem hangs up\n"
"nomodem 	Do not do modem control\n"
"bs, nobs	Enable / disable mapping of Delete to Backspace\n"
"del, nodel	Enable / disable mapping of Backspace to Delete\n"
"maplf, nomaplf	Enable / disable mapping of LF to CR\n"
"igncr, noigncr	Ignore / output carriage returns\n"
"crlf, nocrlf	Enable / disable sending LF after each CR\n"
"delay=<n>	Add delay of <n> ms after each charachter sent\n"
"crwait=<n>	Add delay of <n> ms after each line sent\n"
"esc=<c> 	Set command mode character to Ctrl/<c>\n"
"ctrl,hex,noctrl	Show control characters as ^n (except tab, CR, LF)\n"

	, cmdchar + '@');
}



/*
 Search for the default serial device.

 Look for a USB device first; failing that, look for the first hard
 wired device. Note that many motherboards include a hardware serial
 device, but it might end in a motherboard header or less.

 For Linux & BSD these are:
	OS		Onboard		USB
	Linux		ttyS0		ttyUSB0
	New BSD		cuau0		cuaU0
	Old BSD		ttyd0		ttyU0
	

 Note that this rather assumes that the existence of a device
 name in /dev indicates the actual existence of a device.

 Since USB devices can come and go, we look at the first four possible
 devices.
 */
static char *
defaultdevice(void) {
	static char devbuf[16];
	int i;

	struct stat sb;
	for(i = 1; i < 4; i++) {
		sprintf(devbuf, "/dev/ttyUSB%d", i);
		if(!stat(devbuf, &sb))		/* Linux USB */
			return devbuf;
		sprintf(devbuf, "/dev/cuaU%d", i);
		if(!stat(devbuf, &sb))		/* New BSD USB */
			return devbuf;
		sprintf(devbuf, "/dev/ttyU%d", i);
		if(!stat(devbuf, &sb))		/* Old BSD USB */
			return devbuf;
	}
	if(!stat("/dev/ttyS0",   &sb))		/* Linux COM1 */
		return "/dev/ttyS0";
	if(!stat("/dev/cuau0",   &sb))		/* New BSD COM1 */
		return "/dev/cuau0";
	if(!stat("/dev/ttyd0",   &sb))		/* Old BSD COM1 */
		return "/dev/ttyd0";
	return 0;				/* None of the above */
}



/*
 Show current setup
 */
static void
showsetup() {
	int i;
	char hn[256];
	gethostname(hn, sizeof(hn) - 1);

	if(!device)
		device = defaultdevice();

	fprintf(stderr, "Host: %s\n", hn);
	fprintf(stderr, "Port: %s\n", device ? device : "unassigned");
	fprintf(stderr, "Communications parameters: ");
	for(i = 0; speeds[i].s && speeds[i].c != ispeed; )
		i++;
	fprintf(stderr, "%d", speeds[i].s);
	if(ospeed != ispeed) {
		for(i = 0; speeds[i].s && speeds[i].c != ospeed; )
			i++;
		fprintf(stderr, "/%d", speeds[i].s);
	}
	if(bits == CS5) fprintf(stderr, " 5");
	if(bits == CS6) fprintf(stderr, " 6");
	if(bits == CS7) fprintf(stderr, " 7");
	if(bits == CS8) fprintf(stderr, " 8");
	if(parity == 0) 		fprintf(stderr, " n");
	if(parity == PARENB)		fprintf(stderr, " e");
	if(parity == (PARENB|PARODD))	fprintf(stderr, " o");
	if(parity == -1)		fprintf(stderr, " m");
	if(parity == -2)		fprintf(stderr, " s");
	if(twostop)	fprintf(stderr, " 2\n");
	else		fprintf(stderr, " 1\n");
	fprintf(stderr, "Flow/modem control:");
	if(xonflow)	fprintf(stderr, " xon");
	if(ctsflow)	fprintf(stderr, " cts");
	if(modemcontrol)fprintf(stderr, " modem"); 
	putc('\n', stderr);
	fprintf(stderr, "Character mappings:");
	if(backspace == 8)	fprintf(stderr, " bs");
	if(backspace == 127)	fprintf(stderr, " del");
	if(maplf)		fprintf(stderr, " maplf");
	if(ignorecr)		fprintf(stderr, " igncr");
	if(crlf)		fprintf(stderr, " crlf");
	if(showspecial == 1)	fprintf(stderr, " ctrl");
	if(showspecial == 2)	fprintf(stderr, " hex");
	putc('\n', stderr);
	printf("Modem control: DTR: %s RTS: %s CTS: %s DSR: %s DCD: %s\n",
			setmodem & TIOCM_DTR ? "on" : "off",
			setmodem & TIOCM_RTS ? "on" : "off",
			   modem & TIOCM_CTS ? "on" : "off",
			   modem & TIOCM_DSR ? "on" : "off",
			   modem & TIOCM_CD  ? "on" : "off");
	fprintf(stderr, "Escape character: ^%c\n", cmdchar + '@');
	fflush(stderr);
}


/*
 Use the rzsz or lrzsz package to do file transfer
 Mode should be "rx", "rb" or "rz" to receive a file, "sx", "sb" or "sz"
 to send.  "rz" & "rb" don't require a file name; the others do.
 */
static int
rzsz(char *mode, char *file) {
	static char *loc[] = {	"/usr/bin/",  "/usr/local/bin/l",
				"/usr/bin/l", "/usr/local/bin/", 0 };
	char path[128];
	char *cmd;
	int i;
	struct stat sb;
	pid_t pid;

	/*
	 Check file name
	 */
	if(*file == 0 && (mode[0] != 'r' || mode[1] == 'x')) {
		fprintf(stderr, "File name required for %c%c\n", 
						mode[0], mode[1]);
		return -1;
	}

	/*
	 Find the appropriate command
	 */
	for(i = 0; loc[i]; i++) {
		sprintf(path, "%s%c%c", loc[i], mode[0], mode[1]);
		if(!stat(path, &sb) && (sb.st_mode & S_IXUSR))
			break;
	}
	if(!loc[i]) {
		fprintf(stderr, "RZ/SZ not installed\n");
		return -1;
	}

	/*
	 Get the command name
	 */
	cmd = strrchr(path, '/');
	if(cmd)	cmd++;
	else	cmd = path;

	/*
	 Fork subprocess.  Set stdin & stdout to the serial line
	 stderr remains on the console for progress messages
	 */
	pid = fork();
	if(pid == 0) {
		dup2(fd, 0);
		dup2(fd, 1);
		if(*file) execl(path, cmd, file, NULL);
		else	  execl(path, cmd, NULL);
		_exit(127);
	}
	else if(pid == -1) {
		perror("fork");
		return -1;
	}

	/*
	 Wait for the process to complete and give a meaningful
	 response
	 */
	signal(SIGINT,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	while(waitpid(pid, &i, 0) != pid) {}
	signal(SIGINT,  SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);

	if(WIFSIGNALED(i)) {
		fprintf(stderr, "Terminated on signal %d%s\n", WTERMSIG(i), 
				WCOREDUMP(i) ? " (core dumped)" : "");
		return -1;
	}
	if(WTERMSIG(i))
		return -1;
	return 0;
}



static int readconfig(char *, char *);	/* Forward declaration */

/*
 Process commands passed as strings, may be several commands on a line
 Allow ugliness like 8n1 -- treat as 8, n, 1
 */
static int
setup(char *s, char *cffile, int cfline) {
	char *t, *d;
	int i,j,k;
	int ret = 0;
	struct stat sb;
	char ttybuf[128];
	int justospeed = 0;

	while(*s) {
		/*
		 Skip whitespace, commas
		 */
		while(isspace(*s) || *s == ',') 
			s++;
		if(!*s) break;

		/*
		 '?' = help
		 */
		if(*s == '?') {
			help();
			return -4;
		}

		/*
		 '!' = shell command
		 */
		if(*s == '!' && !noshell) {
			if((t = strchr(s, '\n')))
				*t = 0;
			i = system(++s);
			break;
		}

		/*
		 @! <command>
		 Get commands from shell
		 */
		if(s[0] == '@' && s[1] == '!') {
			if((t = strchr(s, '\n')))
				*t = 0;
			k = readconfig(s+1, 0);
			if(k == -2) {
				if(cffile)
					fprintf(stderr,	"in %s:%d: ",
							cffile, cfline);
				fprintf(stderr, "%s: %s\n", s+1,
							strerror(errno));
				ret = -1;
			}
			break;
		}

		/*
		 File transfer (sx, sz, rx, rz)
		 Run rzsz to perform the actual file transfer
		 Allow "sxfilename", "sx filename" or just "rz"
		 */
		if((s[0] == 's' || s[0] == 'r') && 
		   (s[1] == 'x' || s[1] == 'z')) {
			if((t = strchr(s, '\n')))
				*t = 0;
			for(t = s + 2; isspace(*t); ) t++;
			return rzsz(s, t);
		}

		/*
		 If a number, process it.
		 If it's a valid speed, use that -- if nnn/mmm, set both to nnn 
		 for the first time, and flag things so mmm just sets ospeed
		 5-8 = data bits, 1,2 sets stops
		 Anything else is error
		 */
		if(isdigit(*s)) {
			j = strtol(s, &s, 10);
			for(k = 1; speeds[k].s; k++)
				if(speeds[k].s == j)
					break;
			if(speeds[k].s) {
				ospeed = speeds[k].c;
				if(!justospeed)
					ispeed = ospeed;
				if(*s == '/') {
					s++;
					justospeed = 1;
				}
				else	justospeed = 0;
			}
			else if(j == 5)	bits = CS5;
			else if(j == 6)	bits = CS6;
			else if(j == 7)	bits = CS7;
			else if(j == 8)	bits = CS8;
			else if(j == 1) twostop = 0;
			else if(j == 2) twostop = CSTOPB;
			else {
				if(cffile)
					fprintf(stderr, "in %s:%d: ",
						cffile, cfline);
				fprintf(stderr, "%d: invalid speed/bits\n", j);
				ret = -1;
			}
			if(parity < 0 && bits != CS7)
				parity = 0;
		}

		/*
		 Otherwise, get the alpha-only keyword, see if it matches anything 
		 useful.
		 */
		else {
			t = s;
			while(isalpha(*t)) t++;
			j = *t;
			*t = 0;
			if(	!strcasecmp(s, "xon"))
				xonflow = IXON | IXOFF;
			else if(!strcasecmp(s, "noxon"))
				xonflow = 0;

#ifdef CCTS_OFLOW
			else if(!strcasecmp(s, "cts"))
				ctsflow = CCTS_OFLOW;
			else if(!strcasecmp(s, "nocts"))
				ctsflow = 0;
#endif
			else if(!strcasecmp(s, "modem"))
				modemcontrol = HUPCL;
			else if(!strcasecmp(s, "nomodem"))
				modemcontrol = 0;
			else if(!strcasecmp(s, "E"))
				parity = PARENB;
			else if(!strcasecmp(s, "O"))
				parity = PARENB | PARODD;
			else if(!strcasecmp(s, "M")) {
				parity = -1;
				bits = CS7;
			}
			else if(!strcasecmp(s, "S")) {
				parity = -2;
				bits = CS7;
			}
			else if(!strcasecmp(s, "N"))
				parity = 0;
			else if(!strcasecmp(s, "q") || !strcasecmp(s, "quit"))
				ret = -3;
			else if(!strcasecmp(s, "h") || !strcasecmp(s, "help"))
				help();
			else if(!strcasecmp(s, "del"))
				backspace = 127;
			else if(!strcasecmp(s, "bs"))
				backspace = 8;
			else if(!strcasecmp(s,"nobs") || !strcasecmp(s,"nodel"))
				backspace = 0;
			else if(!strcasecmp(s, "noesc"))
				cmdchar = 0;
			else if(!strcasecmp(s, "maplf"))
				maplf = 1;
			else if(!strcasecmp(s, "nomaplf"))
				maplf = 0;
			else if(!strcasecmp(s, "igncr"))
				ignorecr = 1;
			else if(!strcasecmp(s, "noigncr"))
				ignorecr = 0;
			else if(!strcasecmp(s, "crlf"))
				crlf = 1;
			else if(!strcasecmp(s, "nocrlf"))
				crlf = 0;
			else if(!strcasecmp(s, "noshell"))
				noshell = 1;
			else if(!strcasecmp(s, "b")) 
				sendbreak = 1;
			else if(!strcasecmp(s, "d"))
				setmodem ^= TIOCM_DTR;
			else if(!strcasecmp(s, "r"))
				setmodem ^= TIOCM_RTS;
			else if(!strcasecmp(s, "dtr"))
				setmodem |= TIOCM_DTR;
			else if(!strcasecmp(s, "rts"))
				setmodem |= TIOCM_RTS;
			else if(!strcasecmp(s, "nodtr"))
				setmodem &= ~TIOCM_DTR;
			else if(!strcasecmp(s, "norts"))
				setmodem &= ~TIOCM_RTS;
			else if(!strcasecmp(s, "show"))
				showsetup();
			else if(!strcasecmp(s, "version"))
				fputs(COPYRIGHT, stderr);
			else if(!strcasecmp(s, "ctrl"))
				showspecial = 1;
			else if(!strcasecmp(s, "hex"))
				showspecial = 2;
			else if(!strcasecmp(s, "noctrl"))
				showspecial = 0;
			else if(!strcasecmp(s, "speeds")) {
				for(i = 0; speeds[i].s; i++) {
					if(i && !(i % 4))
						putchar('\n');
					printf("%10d", speeds[i].s);
				}
				putchar('\n');
			}
			/*
			 No?
			 @<filename>	includes a file
			 !cmd		Run a command
			 esc=c		sets escape char
			 <device>	select device
			 */
			else {
				*t = j;
				while(*t && !isspace(*t) && *t != ',')
					t++;
				j = *t;
				*t = 0;
				if(*s == '@') {
					k = readconfig(++s, 0);
					if(k == -2) {
						if(cffile)
							fprintf(stderr,
								"in %s:%d: ",
								cffile, cfline);
						fprintf(stderr, "%s: %s\n",
							s, strerror(errno));
						ret = -1;
					}
					goto next;
				}
				if(!strncasecmp(s, "esc=", 4)) {
					cmdchar = s[4] & 0x1f;
					goto next;
				}
				else if(!strncasecmp(s, "delay=", 6)) {
					delay = atoi(s+6);
					goto next;
				}
				else if(!strncasecmp(s, "crwait=", 7)) {
					linedelay = atoi(s+7);
					goto next;
				}
				d = s;	
				k = stat(d, &sb);
				if(k && *d != '/') {
					sprintf(ttybuf, "/dev/%.100s", d);
					d = ttybuf;
					k = stat(d, &sb);
				}
				if(!k) {
					if((sb.st_mode & S_IFMT) == S_IFCHR) {
						ret = 1;
						device = strdup(d);
						goto next;
					}
				}
				if(cffile)
					fprintf(stderr, "in %s:%d: ",
						cffile, cfline);
				fprintf(stderr,
					"%s: unrecognised keyword/device\n", s);
				ret = -1;
			}
		next:	*t = j;
			s = t;
		}
	}
	return ret;
}


/*
 Read a config file
 Input lines can be lists of config words, or in the form
 name: words
 If name: form, only lines matching passed name are used
 */
static int
readconfig(char *file, char *name) {
	char buf[1024];
	FILE *f;
	char *s, *t;
	int lineno = 0;
	int ret = 0, r;

	/*
	 ~/file = get file from $HOME/file
	 */
	if(!name) name = connname;
	if(*file == '~' && file[1] == '/' && (s = getenv("HOME"))) {
		snprintf(buf, sizeof(buf), "%s/%s", s, file+2);
		file = buf;
	}

	/*
	 Return -2 if can't open
	 */
	if(*file == '!' && !noshell)
		f = popen(++file, "r");
	else	f = fopen(file, "r");
	if(!f)	return -2;

	/*
	 Read input, strip # commends
	 Count lines
	 Keep track of return code
	 */
	while(fgets(buf, sizeof(buf), f)) {
		lineno++;
		if((s = strchr(buf, '#')))
			*s = 0;
		for(s = buf; isspace(*s);)
			s++;
		if(!*s) continue;
		for(t = s; *t && *t != ':' && *t != ',' && !isspace(*t); )
			t++;
		if(*t == ':') {
			*t++ = 0;
			if(!name)
				continue;
			if(strcmp(name, s))
				continue;
			s = t;
			connname = name;
		}
		r = setup(s, file, lineno);
		if(r < 0)
			ret = r;
		if(r > 0 && !ret)
			ret = 1;
	}

	fclose(f);
	return ret;
}


/*
 Sleep n milliseconds
 */
static void
millisleep(int n) {
	struct timespec t;
	t.tv_sec = n / 1000;
	t.tv_nsec = (n % 1000) * 1000000;
	nanosleep(&t, 0);
}


/*
 Set up the port
 */
static int
setupport(int fd) {
	int parityflags;
	int modemflags;
	struct termios tio;
	int abits;
	int m;

	/*
	 See what to do with parity
	 */
	markparity = 0;
	parityflags = parity;
	abits = bits;
	if(parity == -1) {
		parityflags = 0;
		markparity = ISTRIP;
		if(bits == CS7)
			abits = CS8;
	}
	else if(parity == -2) {
		parityflags = 0;
		spaceparity = ISTRIP;
		if(bits == CS7)
			abits = CS8;
	}

	/*
	 If no modem control, use local mode
	 */
	modemflags = ctsflow | modemcontrol;
	if(!modemflags)
		modemflags = CLOCAL;

	/*
	 Set the speed and params
	 */
	tcgetattr(fd, &tio);
	tio.c_iflag = IGNBRK | IGNPAR | xonflow | spaceparity | markparity;
	tio.c_cflag = CREAD | HUPCL | modemflags | abits | parityflags;
	tio.c_oflag = 0;
	tio.c_lflag = 0;
	cfsetispeed(&tio, ispeed);
	cfsetospeed(&tio, ospeed);
	if(tcsetattr(fd, TCSAFLUSH, &tio) == -1)
		return -1;

	/*
	 Set up the modem lines
	 */
	ioctl(fd, TIOCMGET, &modem);
	if((modem & (TIOCM_RTS | TIOCM_DTR)) != (setmodem & (TIOCM_RTS |
							     TIOCM_DTR))) {
		m = setmodem 	& (TIOCM_RTS | TIOCM_DTR);
		ioctl(fd, TIOCMBIS, &m);
		m = (~setmodem) & (TIOCM_RTS | TIOCM_DTR);
		ioctl(fd, TIOCMBIC, &m);
	}

	/*
	 Send a break if requested
	 */
	if(sendbreak) {
		ioctl(fd, TIOCSBRK, 0);
		millisleep(500);
		ioctl(fd, TIOCCBRK, 0);
		sendbreak = 0;
	}

	/*
	 Get the current modem status
	 */
	ioctl(fd, TIOCMGET, &modem);
	setmodem = modem;

	return 0;
}


/*
 Set up the modem.  This is a little tricky due to the need to
 ensure the modem does not block before we set it up.
 */
static int
openport(char *device) {
	int fd;

	if((fd = open(device, O_RDWR|O_NONBLOCK, 0)) < 0)
		DIEP(device)
	if(setupport(fd) < 0)
		DIEP(device)
	millisleep(10);
	fcntl(fd, F_SETFL, 0);

	ioctl(fd, TIOCMGET, &modem);
	setmodem = modem;

	setenv("DTERM_PORT", device, 1);
	return fd;
}


/*
 Usage
 */
static void
usage(char *this) {
	fprintf(stderr, "Usage: %s port/setup\n"
			"	'%s help' for help\n", this, this);
	exit(1);
}


#define WRITE(fd,b,l) { if(write(fd,b,l) < 1) DIEP("write") }

int
main(int argc, char **argv) {
	int nfd;
	int i, j, c;
	unsigned char inbuf;
	char buf[1024];
	char *cmdbuf;
	char cbuf[8];
	char *s;
	int done;
	fd_set fds;
	struct timeval delay_tv, *readdelay;

	/*
	 Do the right things depending on whether stdin & stdout are TTYs
	 If the input is a TTY, we need to put it into non-canonical mode
	 (which we'll actually do later); otherwise, default to turning
	 LFs from a file into CRs.  (See [no]maplf setup command.)
	 If the output is a TTY, default to passing CR transparently; 
	 otherwise default to ignoring CR so that output is logged to a
	 files etc nicely.  (See [no]igncr.)
	 */
	istty = isatty(0);
	if(istty) {
		tcgetattr(0, &savetio);
		maplf = 0;
	}
	else 	maplf = 1;
	if(isatty(1))
		ignorecr = 0;
	else	ignorecr = 1;

	/*
	 Read default config, if no private .dtermrc, use /etc/dtermrc
	 */
	setmodem = modem = TIOCM_DTR | TIOCM_RTS;
	if(readconfig("~/.dtermrc", argv[1]) == -2)
		readconfig("/etc/dtermrc", argv[1]);

	/*
	 Parse args
	 If only arg is "help", exit
	 */
	i = 1;
	if(connname) i = 2;
	for(; i < argc; i++) 
		if(setup(argv[i], 0, 0) < 0)
			usage(argv[0]);
	if(argc == 2 && !strcasecmp(argv[1], "help"))
		exit(0);

	/*
	 If no device specified, have a crack at finding a default device.
	 Bail if we couldn't find it.
	 */
	if(!device && !(device = defaultdevice())) {
		fprintf(stderr, "Could not find default device\n");
		usage(argv[0]);
	}

	/*
	 If the controlling TTY is in fact a TTY, set it up
	 */
	if(istty) {
		intio = savetio;
		intio.c_oflag = 0;
		intio.c_lflag = 0;
		intio.c_iflag = savetio.c_iflag & ~(INLCR|IGNCR|ICRNL);

		intio.c_cc[VEOF]	= _POSIX_VDISABLE;
		intio.c_cc[VEOL]	= _POSIX_VDISABLE;
		intio.c_cc[VEOL2]	= _POSIX_VDISABLE;
		intio.c_cc[VERASE]	= _POSIX_VDISABLE;
		intio.c_cc[VWERASE]	= _POSIX_VDISABLE;
		intio.c_cc[VKILL]	= _POSIX_VDISABLE;
		intio.c_cc[VREPRINT]	= _POSIX_VDISABLE;
		intio.c_cc[VINTR]	= _POSIX_VDISABLE;
		intio.c_cc[VQUIT]	= _POSIX_VDISABLE;
		intio.c_cc[VSUSP]	= _POSIX_VDISABLE;
#ifdef VDSUSP
		intio.c_cc[VDSUSP]	= _POSIX_VDISABLE;
#endif
		intio.c_cc[VLNEXT]	= _POSIX_VDISABLE;
		intio.c_cc[VDISCARD]	= _POSIX_VDISABLE;
#ifdef VSTATUS
		intio.c_cc[VSTATUS]	= _POSIX_VDISABLE;
#endif
		tcsetattr(0, TCSADRAIN, &intio);
	}

	/*
	 Connect to serial port
	 */
	fd = openport(device);
	if(fd < 0) exit(1);

	/*
	 Main loop
	 */
	readdelay = 0;
	done = 0;
	while(!done) {

		/*
		 Set up the select() call
		 If readdelay is not 0, we're waiting for things to go quiet so we
		 can exit.
		 Errors kill us, execpt for interrupted calls
		 0 return only happens if readdelay is set, so we exit then
		 */
		FD_ZERO(&fds);
		if(!readdelay) 
			FD_SET(0, &fds);
		FD_SET(fd, &fds);
		i = select(fd + 1, &fds, 0,0, readdelay);
		if(i == -1 && errno != EINTR)
			DIEP("select");
		if(i == 0 && readdelay)
			break;

		/*
		 If input from line, read a full buffer in
		 IgnoreCR means sucking CRs out of the buffer (yuck)
		 If EOF (e.g. hangup), exit nicely.
		 */
		if(FD_ISSET(fd, &fds)) {
			i = read(fd, buf, sizeof(buf));
			if(i < 0) 
				DIEP(device);
			if(!i)	break;
			if(showspecial) {
				s = buf;
				do {
					c = 0;
					for(j = 0; j < i; j++) {
						c = (unsigned char) s[j];
						if(showspecial == 2 && (
						   c == '\t' || c > '~'))
							break;
						if(c == '\r' && ignorecr)
							break;
						if((  c < ' '    && c != '\t' 
						   && c != '\r'  && c != '\n') 
						   || (c > '~' && c < 160))
							break;
					}
					if(j) WRITE(1, s, j);
					if(j >= i)
						break;
					if(c == '\r' && ignorecr) {
						/* Do nothing */
					}
					else if(c < 32 && showspecial != 2) {
						cbuf[0] = '^';
						cbuf[1] = c + '@';
						WRITE(1, cbuf, 2);
					}
					else if(c == 127 && showspecial != 2)
						WRITE(1, "[DEL]", 5)
					else {
						sprintf(cbuf, "[%02x]", c);
						WRITE(1, cbuf, 4);
					}
					j++;
					s += j;
					i -= j;
				} while(i > 0);					
			}
			else {
				if(ignorecr) {
					j = 0;
					for(s = buf; s < buf + i; s++) 
						if(*s != '\r')
							buf[j++] = *s;
					i = j;
				}			
				WRITE(1, buf, i);
			}
		}

		/*
		 Input on stdin
		 Read a character
		 If EOF, set readdelay to 1 second
		 */
		if(FD_ISSET(0, &fds)) {
			if(read(0, &inbuf, 1) < 1) {
				delay_tv.tv_sec = 1;
				delay_tv.tv_usec = 0;
				readdelay = &delay_tv;
				continue;
			}
			/*
			 If command character received, read commands 
			 */
			if(inbuf == cmdchar && istty) {
				FIXTTY;
				putchar('\n');
				for(;;) {
#ifdef READLINE
					cmdbuf = readline("dterm> ");
					if(!cmdbuf) {
						putchar('\n');
						return 0;
					}
					if(*cmdbuf) 
						add_history(cmdbuf);
#else
					fprintf(stderr, "dterm> ");
					if(!fgets(buf, sizeof(buf), stdin)) {
						putchar('\n');
						return 0;
					}
					cmdbuf = buf;
#endif /* READLINE */

					if((s = strchr(cmdbuf, '#')))
						*s = 0;
					for(s = cmdbuf; *s; s++)
						if(!isspace(*s)) break;
					if(!*s) break;
					ioctl(fd, TIOCMGET, &modem);
					i = setup(cmdbuf, 0, 0);
					if(i == -3) 
						return 0;
					if(i == 1) {
						nfd = openport(device);
						if(nfd >= 0) {
							close(fd);
							fd = nfd;
						}
					}
					else if(setupport(fd))
						fprintf(stderr,
						      "invalid parameters\n");
				}
				if(istty) tcsetattr(0, TCSADRAIN, &intio);
			}
			/*
			 Otherwise do any processing on the character
			 Add dread high bit disease if mark parity
			 BS <-> DEL mapping
			 LF -> CR mapping for files
			 CR -> CRLF mapping for TTYs
			 */
			else {
				if(markparity)
					inbuf |= 0x80;
				if(spaceparity)
					inbuf &= 0x7f;
				if(backspace && (inbuf == 8 || inbuf == 127))
					inbuf = backspace;
				if(maplf && inbuf == '\n')
					inbuf = '\r';
				WRITE(fd, &inbuf, 1);
				if(crlf && inbuf == '\r') {
					inbuf = '\n';
					WRITE(fd, &inbuf, 1);
				}
				if(linedelay && inbuf == '\r')
					millisleep(linedelay);
				else if(delay)
					millisleep(delay);
			}
		}

	}

	/*
	 Fall out the bottom, cleaning up
	 */
	FIXTTY;
	return 0;
}
