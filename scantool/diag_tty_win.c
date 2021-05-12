/* freediag
 * windows-specific tty code
 * (c) fenugrec 2014-2016
 * GPL3
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty_win.h"

#include <windows.h>
#include <basetsd.h>

extern LARGE_INTEGER perfo_freq;
extern float pf_conv;	//these two are defined in diag_os

//struct tty_int : internal data, one per L0 struct
struct tty_int {
	char *name;	//port name, alloc'd
	HANDLE fd;
	DCB dcb;
};

//diag_tty_open : open specified port for L0
ttyp *diag_tty_open(const char *portname) {
	int rv;
	struct tty_int *wti;
	size_t n = strlen(portname) + 1;
	COMMTIMEOUTS devtimeouts;

	assert(portname);

	if ((rv=diag_calloc(&wti,1))) {
		return diag_pseterr(rv);
	}

	wti->fd = INVALID_HANDLE_VALUE;

	//allocate space for portname name
	if ((rv=diag_malloc(&wti->name, n))) {
		free(wti);
		return diag_pseterr(rv);
	}
	//Now, in case of errors we can call diag_tty_close() on wti since its members are alloc'ed
	strncpy(wti->name, portname, n);

	wti->fd=CreateFile(portname, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
		NULL);
	//File hande is created as non-overlapped. This may change eventually.

	if (wti->fd != INVALID_HANDLE_VALUE) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
			FLFMT "Device %s opened, fd %p\n",
			FL, wti->name, wti->fd);
	} else {
		fprintf(stderr,
			FLFMT "Open of device interface \"%s\" failed: %s\n",
			FL, wti->name, diag_os_geterr(0));
		fprintf(stderr, FLFMT
			"(Make sure the device specified corresponds to the\n", FL );
		fprintf(stderr,
			FLFMT "serial device your interface is connected to.\n", FL);

		diag_tty_close(wti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	//purge & abort everything.
	PurgeComm(wti->fd,PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	//as opposed to the unix diag_tty.c ; this one doesn't save previous commstate. The next program to use the COM port
	//will need to deal with it...

	//We will load the DCB with the current comm state. This way we only need to call GetCommState once during a session
	//and the DCB should contain coherent initial values
	if (! GetCommState(wti->fd, &wti->dcb)) {
		fprintf(stderr, FLFMT "Could not get comm state: %s\n",FL, diag_os_geterr(0));
		diag_tty_close(wti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	//Finally set COMMTIMEOUTS to reasonable values (all in ms) ?
	devtimeouts.ReadIntervalTimeout=30;	//i.e. more than 30ms between received bytes
	devtimeouts.ReadTotalTimeoutMultiplier=5;	//timeout per requested byte
	devtimeouts.ReadTotalTimeoutConstant=20;	// (constant + multiplier*numbytes) = total timeout on read(buf, numbytes)
	devtimeouts.WriteTotalTimeoutMultiplier=0;	//probably useless as all flow control will be disabled ??
	devtimeouts.WriteTotalTimeoutConstant=0;
	if (! SetCommTimeouts(wti->fd,&devtimeouts)) {
		fprintf(stderr, FLFMT "Could not set comm timeouts: %s\n",FL, diag_os_geterr(0));
		diag_tty_close(wti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	return wti;
} //diag_tty_open

/* Close up the TTY and restore. */
void diag_tty_close(ttyp *ttyh) {
	struct tty_int *wti = ttyh;

	if (!wti) return;

	if (wti->name) {
		free(wti->name);
	}

	if (wti->fd != INVALID_HANDLE_VALUE) {
		PurgeComm(wti->fd,PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
		CloseHandle(wti->fd);
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
			FLFMT "diag_tty_close : closing fd %p\n", FL, wti->fd);
	}
	free(wti);

	return;
} //diag_tty_close


/*
 * Set speed/parity etc of tty with settings in pset
 * ret 0 if ok
 */
int
diag_tty_setup(ttyp *ttyh,
	const struct diag_serial_settings *pset) {
	HANDLE devhandle;		//just to clarify code
	struct tty_int *wti = ttyh;
	DCB *devstate;
	COMMPROP supportedprops;
	DCB verif_dcb;

	devhandle = wti->fd;
	devstate = &wti->dcb;

	if (devhandle == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "setup: something is not right\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//optional : verify if device supports the requested settings. Testing required to see if USB->serial bridges support this !
	//i.e. some l0 devices try to set 5bps which isn't supported on some devices.
	//For now let's just check if it supports custom baud rates. This check should be added to diag_tty_open where
	//it would set appropriate flags to allow _l0 devices to adapt their functionality.
	if (! GetCommProperties(devhandle,&supportedprops)) {
		fprintf(stderr, FLFMT "could not getcommproperties: %s\n",FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	//simple test : only check if custom baud rates are supported, but don't abort if they aren't. Just notify user.
	if ( !(supportedprops.dwMaxBaud & BAUD_USER))
		fprintf(stderr, FLFMT "warning : device does not support custom baud rates !\n", FL);



	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
		FLFMT "dev %p; %ubps %d,%d,%d \n",
		FL, (void *)devhandle, pset->speed, pset->databits,
		pset->stopbits, pset->parflag);

	/*
	 * Now load the DCB with the parameters requested.
	 */
	// the DCB (devstate) has been loaded with initial values in diag_tty_open so it should already coherent.
	devstate->BaudRate = pset->speed;
	devstate->fBinary=1;	// always binary mode.
	switch (pset->parflag) {
		case diag_par_n:
			//no parity : disable parity in the DCB
			devstate->fParity=0;
			devstate->Parity=NOPARITY;
			break;
		case diag_par_e:
			devstate->fParity=1;
			devstate->Parity=EVENPARITY;
			break;
		case diag_par_o:
			devstate->fParity=1;
			devstate->Parity=ODDPARITY;
			break;
		default:
			fprintf(stderr,
				FLFMT "bad parity setting used !\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
			break;
	}
	devstate->fOutxCtsFlow=0;
	devstate->fOutxDsrFlow=0;	//disable output flow control
	devstate->fDtrControl=DTR_CONTROL_DISABLE;	//XXX allows to permanently set the DTR line !
	devstate->fDsrSensitivity=0;		//pay no attention to DSR for receiving
	devstate->fTXContinueOnXoff=1;	//probably irrelevant ?
	devstate->fOutX=0;		//disable Xon/Xoff tx flow ctl
	devstate->fInX=0;		//disable XonXoff rx flow ctl
	devstate->fErrorChar=0;	//do not replace data with bad parity
	devstate->fNull=0;		// do not discard null bytes ! on rx
	devstate->fRtsControl=RTS_CONTROL_DISABLE;	//XXX allows to set the RTS line!
	devstate->fAbortOnError=0;		//do not abort transfers on error ?
	devstate->wReserved=0;
	devstate->ByteSize=pset->databits;	//bits per byte
	switch (pset->stopbits) {
		case diag_stopbits_1:
			devstate->StopBits=ONESTOPBIT;
			break;
		case diag_stopbits_2:
			devstate->StopBits=TWOSTOPBITS;
			break;
		default:
			fprintf(stderr, FLFMT "bad stopbit setting used!)\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
			break;
	}
	// DCB in devstate is now filled.
	if (! SetCommState(devhandle, devstate)) {
		fprintf(stderr, FLFMT "Could not SetCommState: %s\n",FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//to be really thorough we do another GetCommState and check that the speed was set properly.
	//This *may* help to detect if the serial port supports non-standard baudrates (5bps being
	//particularly problematic with USB->serial converters)
	//I see no particular reason to check all the other fields though.

	if (! GetCommState(devhandle, &verif_dcb)) {
		fprintf(stderr, FLFMT "Could not verify with GetCommState: %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (verif_dcb.BaudRate != pset->speed) {
		fprintf(stderr, FLFMT "SetCommState failed : speed is currently %u\n", FL, (unsigned int) verif_dcb.BaudRate);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
} //diag_tty_setup

/*
 * Set/Clear DTR and RTS lines, as specified
 * on Win32 this can easily be done by calling EscapeCommFunction twice.
 * This takes around ~15-20us to do; probably with ~10 us skew between setting RTS and DTR.
 * If it proves to be a problem, it's possible to change both RTS and DTR at once by updating
 * the DCB and calling SetCommState. That call would take around 30-40us.
 * Note : passing 1 in dtr or rts means "set DTR/RTS", i.e. positive voltage.
 * ret 0 if ok
 */
int
diag_tty_control(ttyp *ttyh, unsigned int dtr, unsigned int rts) {
	unsigned int escapefunc;
	struct tty_int *wti = ttyh;


	if (wti->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (dtr)
		escapefunc=SETDTR;
	else
		escapefunc=CLRDTR;

	if (! EscapeCommFunction(wti->fd,escapefunc)) {
		fprintf(stderr, FLFMT "Could not change DTR: %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (rts)
		escapefunc=SETRTS;
	else
		escapefunc=CLRRTS;

	if (! EscapeCommFunction(wti->fd,escapefunc)) {
		fprintf(stderr, FLFMT "Could not change RTS: %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
} //diag_tty_control


//diag_tty_write : return # of bytes or <0 if error?
//this is an intimidating function to design.
//test #2 : non-overlapped write (i.e. blocking AKA synchronous).
//
//flush buffers before returning; we could also SetCommMask
//to wait for "EV_TXEMPTY" which would give a better idea
//of when the data has been sent.

ssize_t diag_tty_write(ttyp *ttyh, const void *buf, const size_t count) {
	DWORD byteswritten;
	OVERLAPPED *pOverlap;
	struct tty_int *wti = ttyh;
	pOverlap=NULL;		//note : if overlap is eventually enabled, the CreateFile flags should be adjusted

	if (wti->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (count == 0)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (! WriteFile(wti->fd, buf, count, &byteswritten, pOverlap)) {
		fprintf(stderr, FLFMT "WriteFile error:%s. %u bytes written, %u requested\n", FL, diag_os_geterr(0), (unsigned int) byteswritten, (unsigned) count);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (!FlushFileBuffers(wti->fd)) {
		fprintf(stderr, FLFMT "tty_write : could not flush buffers, %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return byteswritten;
} //diag_tty_write


// diag_tty_read
//attempt to read (count) bytes until (timeout) passes.
//This one returns # of bytes read (if any)
//This is non-overlapped for now i.e. blocking.
//timeouts and incomplete data are not handled properly. This is alpha...
//From the API docs :
// ReadFile returns when the number of bytes requested has been read, or an error occurs.


ssize_t
diag_tty_read(ttyp *ttyh, void *buf, size_t count, unsigned int timeout) {
	DWORD bytesread;
	ssize_t rv=DIAG_ERR_TIMEOUT;
	OVERLAPPED *pOverlap;
	struct tty_int *wti = ttyh;
	pOverlap=NULL;
	COMMTIMEOUTS devtimeouts;

	if ((count == 0) || (timeout == 0)) return DIAG_ERR_BADLEN;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "tty_read: ttyh=%p, fd=%p, len=%zu, t=%u\n",
		FL, (void *)wti, (void *)wti->fd, count, timeout);

	if (wti->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

//	GetCommTimeouts(wti->, &devtimeouts);	//get current timeouts
	//and modify them
	devtimeouts.ReadIntervalTimeout= 0; //disabled
	devtimeouts.ReadTotalTimeoutMultiplier=0;	//timeout per requested byte
	devtimeouts.ReadTotalTimeoutConstant=timeout;	// (tconst + mult*numbytes) = total timeout on read
	devtimeouts.WriteTotalTimeoutMultiplier=0;	//probably useless as all flow control will be disabled ??
	devtimeouts.WriteTotalTimeoutConstant=0;
	if (! SetCommTimeouts(wti->fd, &devtimeouts)) {
		fprintf(stderr, FLFMT "Could not set comm timeouts: %s\n",FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (! ReadFile(wti->fd, buf, count, &bytesread, pOverlap)) {
		fprintf(stderr, FLFMT "ReadFile error: %s\n",FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (bytesread > 0)
		rv=bytesread;
	return rv;

}



/*
 *  flush input buffer and display some of the discarded data
 * ret 0 if ok
 *
 */
int diag_tty_iflush(ttyp *ttyh) {
	uint8_t buf[MAXRBUF];
	int rv;
	struct tty_int *wti = ttyh;

	/* Read any old data hanging about on the port */
	rv = diag_tty_read(wti, buf, sizeof(buf), IFLUSH_TIMEOUT);
	if (rv > 0) {
		//don't dump data : flood
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_DATA, DIAG_DBGLEVEL_V,
			FLFMT "tty_iflush: >=%d junk bytes discarded: 0x%X...\n",
			FL, rv, buf[0]);
	}
	PurgeComm(wti->fd, PURGE_RXABORT | PURGE_RXCLEAR);

	return 0;
}



// diag_tty_break #1 : use Set / ClearCommBreak
// and return as soon as break is cleared.
// ret 0 if ok
int diag_tty_break(ttyp *ttyh, const unsigned int ms) {
	LARGE_INTEGER qpc1, qpc2;	//for timing verification
	long real_t;	//"real" duration
	struct tty_int *wti = ttyh;
	int errval=0;

	if (wti->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (ms <= 1)
		return diag_iseterr(DIAG_ERR_GENERAL);

	QueryPerformanceCounter(&qpc1);
	errval = !SetCommBreak(wti->fd);
	QueryPerformanceCounter(&qpc2);
	//that call can take quite a while (6ms !!) on some setups (win7 + CH340 USB-Serial).
	//It's still impossible to know (from here) when exactly TXD goes low (beginning or end of the call)
	real_t=(long) (pf_conv * (qpc2.QuadPart-qpc1.QuadPart)) / 1000L;
	real_t = (long) ms - real_t;	//time remaining
	if (real_t <= 0) real_t = 0;
	diag_os_millisleep((unsigned int ) real_t);

	errval |= !ClearCommBreak(wti->fd);

	if (errval) {
		//if either of the calls failed
		fprintf(stderr, FLFMT "tty_break could not set/clear break!\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
}


/*
 * diag_tty_fastbreak: send 0x00 at 360bps => fixed 25ms break; return [ms] after starting break.
 * This is for ISO14230 fast init : typically diag_tty_fastbreak(tty_int, 50)
 * It assumes the interface is half-duplex.
 * Ret 0 if ok
 */
int diag_tty_fastbreak(ttyp *ttyh, const unsigned int ms) {
	HANDLE dh;	//just to clarify code
	struct tty_int *wti = ttyh;
	DCB tempDCB; 	//for sabotaging the settings just to do the break
	DCB origDCB;
	LARGE_INTEGER qpc1, qpc2, qpc3;	//to time the break period
	LONGLONG timediff;		//64bit delta
	long int tremain,counts, break_error;

	uint8_t cbuf;
	int xferd;
	DWORD byteswritten;

	dh = wti->fd;
	if (ms<25)		//very funny
		return diag_iseterr(DIAG_ERR_TIMEOUT);

	if (dh == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	GetCommState(dh, &origDCB);
	GetCommState(dh, &tempDCB); //ugly, but a memcpy would be worse

	tempDCB.BaudRate=360;
	tempDCB.ByteSize=8;
	tempDCB.fParity=0;
	tempDCB.Parity=NOPARITY;
	tempDCB.StopBits=ONESTOPBIT;

	if (! SetCommState(dh, &tempDCB)) {
		fprintf(stderr, FLFMT "SetCommState error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Send a 0x00 byte message */

	if (! WriteFile(dh, "\0", 1, &byteswritten, NULL)) {
		fprintf(stderr, FLFMT "WriteFile error:%s\n", FL, diag_os_geterr(0));
		SetCommState(dh, &origDCB);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	//get approx starting time. I think this is the closest we can
	//get to the actual time the byte gets sent since we call FFB
	//right after.
	QueryPerformanceCounter(&qpc1);

	if (!FlushFileBuffers(dh)) {
		fprintf(stderr, FLFMT "FFB error, %s\n", FL, diag_os_geterr(0));
		SetCommState(dh, &origDCB);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}


	/*
	 * And read back the single byte echo, which shows TX completes
 	 */
	xferd = diag_tty_read(wti, &cbuf, 1, ms + 20);

	//we'll usually have a few ms left to wait; we'll use this
	//to restore the port settings

	if (! SetCommState(dh, &origDCB)) {
		fprintf(stderr, FLFMT "tty_fastbreak: could not restore setting: %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//Not getting the echo byte doesn't mean fastbreak has necessarily
	// failed. But we really should be getting an echo back...
	if (xferd < 0)
		return diag_iseterr(xferd);
	if ((xferd == 0) || (cbuf != 0)) {
		/* Error, EOF or bad echo */
		fprintf(stderr, FLFMT "Did not get fastbreak echo!\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}


	QueryPerformanceCounter(&qpc2);		//get current time,
	timediff=qpc2.QuadPart-qpc1.QuadPart;	//elapsed counts since diag_tty_write
	counts=(ms*perfo_freq.QuadPart)/1000;		//total # of counts for requested tWUP
	tremain=counts-timediff;	//counts remaining
	if (tremain<=0)
		return 0;

	tremain = ((LONGLONG) tremain*1000)/perfo_freq.QuadPart;	//convert to ms; imprecise but that should be OK.
	diag_os_millisleep((unsigned int) tremain);
	QueryPerformanceCounter(&qpc3);

	timediff=qpc3.QuadPart-qpc1.QuadPart;	//total cycle time.
	break_error= (long) timediff - counts;	//real - requested
	break_error= (long) (break_error * pf_conv);	//convert to us !
	if (break_error > 1000 || break_error < -1000)
		fprintf(stderr, FLFMT "tty_fastbreak: tWUP out of spec by %ldus!\n", FL, break_error);


	return 0;
}	//diag_tty_fastbreak

/* Find valid serial ports.
 * Adapted from FreeSSM :
 * https://github.com/Comer352L/FreeSSM
 */

char **diag_tty_getportlist(int *numports) {
	HKEY hKey;				// handle to registry key
	DWORD index = 0;			// index registry-key: unsigned int (32bit)
	char ValueName[256] = "";
	unsigned long szValueName = 256;	// variable that specifies the size (in characters, including the terminating null char) of the buffer pointed to by the "ValueName" parameter.
	unsigned char Data[256] = "";		// buffer that receives the data for the value entry. This parameter can be NULL if the data is not required
	unsigned long szData = 256;		// variable that specifies the size, in bytes, of the buffer pointed to by the lpData parameter.
	long cv;
	HANDLE hCom_t = NULL;

	char **portlist=NULL;
	int elems=0;		//temp number of ports found

	assert(numports != NULL);
	*numports = 0;

	// OPEN REGISTRY-KEY AND BROWSE ENTRYS:
	cv = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey);
	if (cv == ERROR_SUCCESS) {
		while ((RegEnumValueA(hKey, index, ValueName, &szValueName, NULL, NULL,Data,&szData)) == ERROR_SUCCESS) {
			if (!strncmp((char *)Data,"COM",3)) {
				// CHECK IF PORT IS AVAILABLE (not in use):
				char NTdevName[30] = "\\\\.\\";	// => "\\.\"
				strncpy(NTdevName+4, (char *)Data, 25);
				/* NOTE: MS-DOS device names ("COMx") are not reliable if x is > 9 !!!
					=> device can not be opened (error 2 "The system cannot find the file specified.")
					Using NT device names instead ("\\.\COMx") which work in all cases.
				*/
				hCom_t = CreateFileA(NTdevName,				// device name of the port
							GENERIC_READ | GENERIC_WRITE,	// read/write access
							0,					// must be opened with exclusive-access
							NULL,				// default security attributes
							OPEN_EXISTING,			// must use OPEN_EXISTING
							0,					// not overlapped I/O
							NULL				// must be NULL for comm devices
						 );
				if (hCom_t != INVALID_HANDLE_VALUE) {
					CloseHandle(hCom_t);
					char **templist = strlist_add(portlist, NTdevName, elems);
					if (!templist) {
						strlist_free(portlist, elems);
						return diag_pseterr(DIAG_ERR_NOMEM);
					}
					portlist = templist;
					elems++;
				}
			}
			szValueName = 256;		// because RegEnumValue has changed value
			szData = 256;			// because RegEnumValue has changed value
			index++;
		}	//while
		//std::sort(portlist.begin(), portlist.end());	// quicksort from <algorithm>
		(void) RegCloseKey(hKey);
	}

	*numports = elems;
	return portlist;
}
