
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_err.h"
#include "diag_tty.h"

#include <windows.h>

extern LARGE_INTEGER perfo_freq;
extern float pf_conv;	//these two are defined in diag_os


//diag_tty_open : load the diag_l0_device with the correct stuff
int diag_tty_open(struct diag_l0_device **ppdl0d,
	const char *subinterface,
	const struct diag_l0 *dl0,
	void *dl0_handle)
{
	int rv;
	struct diag_l0_device *dl0d;
	size_t n = strlen(subinterface) + 1;
	COMMTIMEOUTS devtimeouts;

	if ((rv=diag_calloc(&dl0d, 1)))		//free'd in diag_tty_close
		return diag_iseterr(rv);

	dl0d->fd = INVALID_HANDLE_VALUE;
	dl0d->dl0_handle = dl0_handle;
	dl0d->dl0 = dl0;

	if ((rv=diag_calloc(&dl0d->ttystate, 1))) {
		free(dl0d);
		return diag_iseterr(rv);
	}

	*ppdl0d = dl0d;

	//allocate space for subinterface name
	if ((rv=diag_malloc(&dl0d->name, n))) {
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(rv);
	}
	strncpy(dl0d->name, subinterface, n);

	dl0d->fd=CreateFile(subinterface, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	//File hande is created as non-overlapped. This may change eventually.

	if (dl0d->fd != INVALID_HANDLE_VALUE) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr, FLFMT "Device %s opened, fd %p\n",
				FL, dl0d->name, dl0d->fd);
	} else {
		fprintf(stderr,
			FLFMT "Open of device interface \"%s\" failed: %s\n",
			FL, dl0d->name, diag_os_geterr(0));
		fprintf(stderr, FLFMT
			"(Make sure the device specified corresponds to the\n", FL );
		fprintf(stderr,
			FLFMT "serial device your interface is connected to.\n", FL);

		diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//purge & abort everything.
	PurgeComm(dl0d->fd,PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	//as opposed to the unix diag_tty.c ; this one doesn't save previous commstate. The next program to use the COM port
	//will need to deal with it...

	//We will load the DCB with the current comm state. This way we only need to call GetCommState once during a session
	//and the DCB should contain coherent initial values
	if (! GetCommState(dl0d->fd, dl0d->ttystate)) {
		fprintf(stderr, FLFMT "Could not get comm state: %s\n",FL, diag_os_geterr(0));
		diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//Finally set COMMTIMEOUTS to reasonable values (all in ms) ?
	devtimeouts.ReadIntervalTimeout=30;	//i.e. more than 30ms between received bytes
	devtimeouts.ReadTotalTimeoutMultiplier=5;	//timeout per requested byte
	devtimeouts.ReadTotalTimeoutConstant=20;	// (constant + multiplier*numbytes) = total timeout on read(buf, numbytes)
	devtimeouts.WriteTotalTimeoutMultiplier=0;	//probably useless as all flow control will be disabled ??
	devtimeouts.WriteTotalTimeoutConstant=0;
	if (! SetCommTimeouts(dl0d->fd,&devtimeouts)) {
		fprintf(stderr, FLFMT "Could not set comm timeouts: %s\n",FL, diag_os_geterr(0));
		diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
} //diag_tty_open

/* Close up the TTY and restore. */
void diag_tty_close(struct diag_l0_device **ppdl0d)
{
	if (ppdl0d) {
		struct diag_l0_device *dl0d = *ppdl0d;
		if (dl0d) {
			if (dl0d->ttystate) {
				free(dl0d->ttystate);
				dl0d->ttystate = NULL;
			}

			if (dl0d->name) {
				free(dl0d->name);
				dl0d->name = NULL;
			}
			if (dl0d->fd != INVALID_HANDLE_VALUE) {
				PurgeComm(dl0d->fd,PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
				CloseHandle(dl0d->fd);
				dl0d->fd = INVALID_HANDLE_VALUE;
			}
			free(dl0d);
			*ppdl0d = NULL;
		}
	}
	return;
} //diag_tty_close


//XXX why do we need a function for these?
void *
diag_l0_dl0_handle(struct diag_l0_device *dl0d) {
	return dl0d->dl0_handle;
}

struct diag_l2_link *
diag_l0_dl2_link(struct diag_l0_device *dl0d) {
	return dl0d->dl2_link;
}

void
diag_l0_set_dl2_link(struct diag_l0_device *dl0d,
	struct diag_l2_link *dl2_link) {
	dl0d->dl2_link = dl2_link;
}


/*
 * Set speed/parity etc of dl0d with settings in pset
 * ret 0 if ok
 */
int
diag_tty_setup(struct diag_l0_device *dl0d,
	const struct diag_serial_settings *pset)
{
	HANDLE devhandle=dl0d->fd;		//used just to clarify code
	DCB *devstate=dl0d->ttystate;
	COMMPROP supportedprops;
	DCB verif_dcb;

	if (devhandle == INVALID_HANDLE_VALUE || dl0d->ttystate == 0) {
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



	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
	{
		fprintf(stderr, FLFMT "device handle=%p; ",
			FL, (void *)devhandle);
		fprintf(stderr, "speed=%d databits=%d stopbits=%d parity=%d\n",
			pset->speed, pset->databits, pset->stopbits, pset->parflag);
	}

	/*
	 * Now load the DCB with the paramaters requested.
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
diag_tty_control(struct diag_l0_device *dl0d,  unsigned int dtr, unsigned int rts)
{
	LARGE_INTEGER perftest;	// making sure the frequency is the same
	unsigned int escapefunc;

	if (dl0d->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	QueryPerformanceFrequency(&perftest);
	if (perfo_freq.QuadPart != perftest.QuadPart) {
		//this sanity check takes ~ 4us on my system
		fprintf(stderr, FLFMT "Warning: your system changed its performance counter frequency !\n", FL);
		perfo_freq.QuadPart = perftest.QuadPart;
		//we update the global setting, since it's just use for validation at the moment (not control)
	}

	if (dtr)
		escapefunc=SETDTR;
	else
		escapefunc=CLRDTR;

	if (! EscapeCommFunction(dl0d->fd,escapefunc)) {
		fprintf(stderr, FLFMT "Could not change DTR: %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (rts)
		escapefunc=SETRTS;
	else
		escapefunc=CLRRTS;

	if (! EscapeCommFunction(dl0d->fd,escapefunc)) {
		fprintf(stderr, FLFMT "Could not change RTS: %s\n", FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (diag_l0_debug & (DIAG_DEBUG_TIMER | DIAG_DEBUG_IOCTL)) {
		fprintf(stderr, FLFMT "~@%lums : DTR/RTS changed\n", FL, (unsigned long) GetTickCount());
	}

	return 0;
} //diag_tty_control


//diag_tty_write : return # of bytes or <0 if error?
//this is an intimidating function to design.
//test #1 :  non-overlapped write (i.e. blocking AKA synchronous), until I know what I'm doing.
//It may be useful to make this overlapped instead : that way the caller regains control
// at the approx moment the actual transmission would begin.  As it stands, there is no
// correlation between the moment WriteFile returns and when transmission actually
// takes place. I..e. WriteFile will probably return after dumping the bytes in the
// UART's buffer (most likely) which doesn't mean they were sent yet.
// *However*, changing this to an Overlapped operation has wide implications on the
// rest of the code, and may require a lot of changes to the UNIX tty stuff too.

ssize_t diag_tty_write(struct diag_l0_device *dl0d, const void *buf, const size_t count) {
	DWORD byteswritten;
	OVERLAPPED *pOverlap;
	pOverlap=NULL;		//note : if overlap is eventually enabled, the CreateFile flags should be adjusted

	if (dl0d->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (! WriteFile(dl0d->fd, buf, count, &byteswritten, pOverlap)) {
		fprintf(stderr, FLFMT "WriteFile error:%s. %u bytes written, %u requested\n", FL, diag_os_geterr(0), (unsigned int) byteswritten, count);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "wrote %u bytes out of %d\n", FL, (unsigned int) byteswritten, count );
	}

	return byteswritten;
} //diag_tty_write


// diag_tty_read
//this is also scary.
//attempt to read (count) bytes until (timeout) passes.
//Note : setting timeout to 0 is like setting an infinite timeout on win32.
//TODO : unify diag_tty_read timeouts between win32 and unix
//This is non-overlapped for now; it's also probably broken;
//timeouts and incomplete data are not handled properly. This is not even pre-alpha.

ssize_t
diag_tty_read(struct diag_l0_device *dl0d, void *buf, size_t count, int timeout) {
	DWORD bytesread;
	OVERLAPPED *pOverlap;
	pOverlap=NULL;
	COMMTIMEOUTS devtimeouts;

	if (dl0d->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

//	GetCommTimeouts(dl0d->fd, &devtimeouts);	//get current timeouts
	//and modify them
	devtimeouts.ReadIntervalTimeout=0;	//disabled
	devtimeouts.ReadTotalTimeoutMultiplier=0;	//timeout per requested byte
	devtimeouts.ReadTotalTimeoutConstant=timeout;	// (tconst + mult*numbytes) = total timeout on read
	devtimeouts.WriteTotalTimeoutMultiplier=0;	//probably useless as all flow control will be disabled ??
	devtimeouts.WriteTotalTimeoutConstant=0;
	if (! SetCommTimeouts(dl0d->fd,&devtimeouts)) {
		fprintf(stderr, FLFMT "Could not set comm timeouts: %s\n",FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (! ReadFile(dl0d->fd, buf, count, &bytesread, pOverlap)) {
		fprintf(stderr, FLFMT "ReadFile error: %s\n",FL, diag_os_geterr(0));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return bytesread;

}



/*
 *  flush input buffer and display some of the discarded data
 * ret 0 if ok
 * a short timeout (30ms) is used in case the caller needs to receive a byte soon (like after a iso9141 slow init)
 *
 */
int diag_tty_iflush(struct diag_l0_device *dl0d)
{
	uint8_t buf[MAXRBUF];
	int rv;

	/* Read any old data hanging about on the port */
	rv = diag_tty_read(dl0d, buf, sizeof(buf), IFLUSH_TIMEOUT);
	if ((rv > 0) && (diag_l0_debug & DIAG_DEBUG_DATA)) {
		fprintf(stderr, FLFMT "tty_iflush: at least %d junk bytes discarded: 0x%x ... ", FL, rv, buf[0]);
		// diag_data_dump(stderr, (void *) buf, (size_t) rv); //this could take a long time.
		fprintf(stderr,"\n");
	}
	PurgeComm(dl0d->fd, PURGE_RXABORT | PURGE_RXCLEAR);

	return 0;
}



//different tty_break implementations.
//ret 0 if ok

// diag_tty_break #1 : use Set / ClearCommBreak
// and return as soon as break is cleared.
int diag_tty_break(struct diag_l0_device *dl0d, const unsigned int ms) {
	LARGE_INTEGER qpc1, qpc2, perftest;	//for timing verification
	static long correction=0;	//running average offset (us) to add to the timeout
	long real_t;	//"real" duration measured in us
	int errval=0;

	if (dl0d->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if ( (ms + correction/1000)<1)
		return diag_iseterr(DIAG_ERR_GENERAL);

	QueryPerformanceFrequency(&perftest);
	if (perfo_freq.QuadPart != perftest.QuadPart) {
		//this sanity check takes ~ 4us on my system
		fprintf(stderr, FLFMT "Warning: your system changed its performance counter frequency !\n", FL);
		perfo_freq.QuadPart = perftest.QuadPart;
		pf_conv=1.0E6 / perftest.QuadPart;
		//we update the global settings, since it's just use for validation at the moment (not control)
	}

	QueryPerformanceCounter(&qpc1);
	errval=!SetCommBreak(dl0d->fd);
	diag_os_millisleep(ms + correction/1000);	//probably the most inadequate way of doing this on win32 platforms, but it might work
	QueryPerformanceCounter(&qpc2);
	errval += !ClearCommBreak(dl0d->fd);

	if (errval) {
		//if either of the calls failed
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	real_t=(long) (pf_conv * (qpc2.QuadPart-qpc1.QuadPart));
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "diag_tty_break: duration = %ldus.\n",
			FL, real_t);
	}
	//now verify if it's within 1ms of the requested delay.
	real_t = real_t - (ms*1000);
	if ((real_t < -1000) || (real_t > 1000)) {
		//correct by half of the error.
		correction = correction - (real_t / 2);
		fprintf(stderr, FLFMT "Warning : break duration out of spec by %ldus ! New correction offset=%ldus.\n",
			FL, real_t, correction);
	}

	return 0;
}


/*
 * diag_tty_fastbreak: send 0x00 at 360bps => fixed 25ms break; return [ms] after starting break.
 * This is for ISO14230 fast init : typically diag_tty_fastbreak(dl0d, 50)
 */
int diag_tty_fastbreak(struct diag_l0_device *dl0d, const unsigned int ms)
{
	if (ms<25)		//very funny
		return diag_iseterr(DIAG_ERR_TIMEOUT);

	DCB tempDCB; 	//for sabotaging the settings just to do the break
	DCB origDCB;
	LARGE_INTEGER qpc1, qpc2, qpc3;	//to time the break period
	LONGLONG timediff;		//64bit delta
	long int tremain,counts;

	char cbuf;
	int xferd;

	if (dl0d->fd == INVALID_HANDLE_VALUE) {
		fprintf(stderr, FLFMT "Error. Is the port open ?\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	GetCommState(dl0d->fd, &origDCB);
	GetCommState(dl0d->fd, &tempDCB); //I didn't feel like memcpy-ing one structure to the other...

	tempDCB.BaudRate=360;
	tempDCB.ByteSize=8;
	tempDCB.fParity=0;
	tempDCB.Parity=NOPARITY;
	tempDCB.StopBits=ONESTOPBIT;

	if (! SetCommState(dl0d->fd, &tempDCB)) {
		fprintf(stderr, FLFMT "SetCommState error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Send a 0x00 byte message */
	QueryPerformanceCounter(&qpc1);		//get starting time
	diag_tty_write(dl0d, "\0", 1);

	/*
	 * And read back the single byte echo, which shows TX completes
	 * (this is because of the half-duplex nature of the K line)
 	 */
	while ( (xferd = diag_tty_read(dl0d, &cbuf, 1, ms<<2)) <= 0)
	{
		if (xferd == DIAG_ERR_TIMEOUT)
			return diag_iseterr(DIAG_ERR_TIMEOUT);
		if (xferd == 0)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF.\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
	}
	//we'll usually have a few ms left to wait; we'll use this
	//to restore the port settings.

	if (! SetCommState(dl0d->fd, &origDCB)) {
		fprintf(stderr, FLFMT "tty_break: could not restore setting: %s\n", FL, diag_os_geterr(0));
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
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "tty_fastbreak: tWUP=%ldus (requested=%u)\n", FL, (long)(pf_conv*timediff),ms);
	}


	return 0;
}	//diag_tty_fastbreak


