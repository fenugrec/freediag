/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * "nisprog" module
 * (c) 2014-2015 fenugrec
 * Licensed under GPLv3
 *
 * This is an add-in sub-command for the "diag" command implemented in scantool_diag.c ;
 * It adds some Nissan-specific commands and tests.
 * Its presence is temporary until it gets forked to a stand-alone utility, probably
 * linking to libdiag.
 */


//cmd_diag_nisprog : test Nissan-specific SIDs.
//This is experimental, and only tested on a handful of ECUs. So far it hasn't caused any permanent damage.
//
//My goal is to make a "nisprog" standalone program
//that compiles against libdiag and packages these and other features, aimed
//at dumping + reflashing ROMs.
//
//As-is, this is hack quality (low) code that "trespasses" levels to go faster, skips some
//checks, and is generally not robust. But it does work on a few different hardware setups and ECUs.
//Uses the global l2 connection.


#include <stdbool.h>
#include "diag_os.h"
#include "diag_tty.h"	//for setspeed
#include "diag_l2_iso14230.h" 	//needed to force header type (nisprog)

#define NP_RX_EXTRATIMEOUT 20	//ms, added to all timeouts. Adjust to eliminiate read timeout errors
#define NPK_SPEED 62500	//bps default speed for npkern kernel

/** fwd decls **/
static int npkern_init(void);
uint32_t read_ac(uint8_t *dest, uint32_t raddr, uint32_t len);

/****/

uint32_t readinvb(const uint8_t *buf) {
	// ret 4 bytes at *buf with SH endianness
	// " reconst_4B"
	return buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
}

void writeinvb(uint32_t val, uint8_t *buf) {
	//write 4 bytes at *buf with SH endianness
	// "decomp_4B"
	buf += 3;	//start at end
	*(buf--) = val & 0xFF;
	val >>= 8;
	*(buf--) = val & 0xFF;
	val >>= 8;
	*(buf--) = val & 0xFF;
	val >>= 8;
	*(buf--) = val & 0xFF;

	return;
}

void genkey2(const uint8_t *seed8, uint8_t *key) {
	//this uses the kline_at algo... niskey2.c
	//writes 4 bytes in buffer *key
	uint32_t seed, ecx, xorloops;
	int ki;

	const uint32_t keytable[]={0x14FA3579, 0x27CD3964, 0x1777FE32, 0x9931AF12,
		0x75DB3A49, 0x19294CAA, 0x0FF18CD76, 0x788236D,
		0x5A6F7CBB, 0x7A992254, 0x0ADFD5414, 0x343CFBCB,
		0x0C2F51639, 0x6A6D5813, 0x3729FF68, 0x22A2C751};

	seed = readinvb(seed8);

	ecx = (seed & 1)<<6 | (seed>>9 & 1)<<4 | (seed>>1 & 1)<<3;
	ecx |= (seed>>11 & 1)<<2 | (seed>>2 & 1)<<1 | (seed>>5 & 1);
	ecx += 0x1F;

	if (ecx <= 0) {
		printf("problem !!\n");
		return;
	}

	ki = (seed & 1)<<3 | (seed>>1 & 1)<<2 | (seed>>2 & 1)<<1 | (seed>>9 & 1);

	//printf("starting xorloop with ecx=0x%0X, ki=0x%0X\n", ecx, ki);

	for (xorloops=0; xorloops < ecx; xorloops++) {
		if (seed & 0x80000000) {
			seed += seed;
			seed ^= keytable[ki];
		} else {
			seed += seed;
		}
	}
	//here, the generated key is in "seed".

	writeinvb(seed, key);

	return;
}

void genkey1(const uint8_t *seed8, uint32_t m, uint8_t *key) {
	//this uses the NPT_DDL2 algo (with key-in-ROM) ... niskey1.c
	//writes 4 bytes in buffer *key

	//m: scrambling code (hardcoded in ECU firmware)
	//seed is pseudorandom generated with SID 27 01
	uint32_t seed;
	uint16_t mH,mL, sH, sL;
	uint16_t kL, kH;	//temp words

	uint16_t var2,var2b, var6;
	uint32_t var3;

	uint16_t var7,var8, var9;
	uint32_t var10, var10b;

	mL = m & 0xFFFF;
	mH= m >> 16;
	seed = readinvb(seed8);
	sL = seed & 0xFFFF;
	sH = seed >> 16;

	var2 = (mH + sL);	// & 0xFFFF;
	var3 = var2 << 2;
	var6 = (var3 >>16);
	var2b = var6 + var2 + var3 -1;

	kL = var2b ^ sH;

	var7 = (mL + kL) ;
	var10 = var7 << 1;
	var8 = (var10 >>16) + var7 + var10 -1;
	var10b = var8 << 4;
	var9 = var10b + (var10b >>16);
	kH = sL ^ var9 ^ var8;

	writeinvb((kH << 16) | kL, key);	//write key in buffer.
	return;
}

//np 7 <scode>: same as np 6 but with the NPT_DDL algo.

//np 1: try start diagsession, Nissan Repro style +
// accesstimingparams (get limits + setvals)
static int np_1(UNUSED(int argc), UNUSED(char **argv)) {

	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x10;
	txdata[1]=0x85;
	txdata[2]=0x14;
	nisreq.len=3;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return CMD_FAILED;
	if (rxmsg->data[0] != 0x50) {
		printf("got bad response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return CMD_FAILED;
	}
	printf("StartDiagsess: got ");
	diag_data_dump(stdout, rxmsg->data, rxmsg->len);
	diag_freemsg(rxmsg);

	//try accesstimingparam : read limits
	txdata[0]=0x83;
	txdata[1]=0x0;	//read limits
	nisreq.len=2;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return CMD_FAILED;
	printf("\nAccesTiming : read limits got ");
	diag_data_dump(stdout, rxmsg->data, rxmsg->len);
	diag_freemsg(rxmsg);

	//try ATP : read settings
	txdata[0]=0x83;
	txdata[1]=0x02;
	nisreq.len=2;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return CMD_FAILED;
	printf("\nAccesTiming : read settings got ");
	diag_data_dump(stdout, rxmsg->data, rxmsg->len);
	diag_freemsg(rxmsg);
	printf("\n");
	return CMD_OK;
}
//np 2 :
static int np_2(int argc, char **argv) {
	//np 2 <addr> : read 1 byte @ addr, with SID A4
	// TX {07 A4 <A0> <A1> <A2> <A3> 04 01 cks}, 9 bytes on bus
	// RX {06 E4 <A0> <A1> <A2> <A3> <BB> cks}, 8 bytes
	// total traffic : 17 bytes for 1 rx'd byte - very slow
	//printf("Attempting to read 1 byte @ 000000:\n");
	uint8_t txdata[64];	//data for nisreq
	uint32_t addr;
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	if (argc != 3) {
		printf("usage: np 2 <addr>: read 1 byte @ <addr>\n");
		return CMD_USAGE;
	}
	if (sscanf(argv[2], "%x", &addr) != 1) {
		printf("Did not understand %s\n", argv[2]);
		return CMD_USAGE;
	}
	txdata[0]=0xA4;
	txdata[4]= (uint8_t) (addr & 0xFF);
	txdata[3]= (uint8_t) (addr >> 8) & 0xFF;
	txdata[2]= (uint8_t) (addr >> 16) & 0xFF;
	txdata[1]= (uint8_t) (addr >> 24) & 0xFF;
	txdata[5]=0x04;	//TXM
	txdata[6]=0x01;	//NumResps
	nisreq.len=7;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return CMD_FAILED;
	if ((rxmsg->data[0] != 0xE4) || (rxmsg->len != 6)) {
		printf("got bad A4 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return CMD_FAILED;
	}
	printf("Got: 0x%02X\n", rxmsg->data[5]);
	diag_freemsg(rxmsg);
	return CMD_OK;
}


/* np 4 : dump <len> bytes @<start> to already-opened <outf>;
 * uses read_ac() (std L2 request + recv mechanism)
 */
static int np_4(FILE *outf, uint32_t start, uint32_t len) {
	int retryscore = 100;	/* iteration failures decrease this; success increases it up to 100. Abort when 0. */

	if (!outf) return CMD_FAILED;

	/* cheat : we know read_ac is faster (less overhead) for multiples of 12,
	 * so read 12*16 = 192B chunks.
	 */
	while ((len > 0) && (retryscore > 0)) {
		uint8_t tbuf[12*16];
		uint32_t rsize;
		uint32_t res;
		unsigned long chrono;

		chrono=diag_os_getms();
		rsize = MIN(len, ARRAY_SIZE(tbuf));
		res = read_ac(tbuf, start, rsize);
		if (res != rsize) {
			/* partial read; not necessarily fatal */
			retryscore -= 25;
			diag_os_millisleep(300);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
		}
		diag_data_dump(stderr, tbuf, res);
		if (fwrite(tbuf, 1, res, outf) != res) {
			/* partial write; bigger problem. */
			return CMD_FAILED;
		}
		chrono = diag_os_getms() - chrono;
		if (!chrono) chrono += 1;

		retryscore += (retryscore <= 95)? 5:0;

		len -= res;
		start += res;
		if (res) {
			printf("%u bytes remaining @ ~%lu Bps = %lu s.\n", len, (1000 * res) / chrono,
				len * chrono / (res * 1000));
		}
	}
	if (retryscore <= 0) {
		//there was an error inside and no retries left
		printf("Too many errors, no more retries @ addr=%08X.\n", start);
		return CMD_FAILED;
	}
	return CMD_OK;
}


/* np 5 : fast dump <len> bytes @<start> to already-opened <outf>;
 * uses fast read technique (receive from L1 direct)
 */
static int np_5(FILE *outf, const uint32_t start, uint32_t len) {
		//SID AC + 21 technique.
		// AC 81 {83 GGGG} {83 GGGG} ... to load addresses, (5*n + 4) bytes on bus
		// RX: {EC 81}, 4 bytes
		// TX: {21 81 04 01} to dump data (6 bytes)
		// RX: {61 81 <n*data>} (4 + n) bytes.
		// Total traffic : (6*n + 18) bytes on bus for <n> bytes RX'd
	struct diag_msg nisreq={0};	//request to send
	uint8_t txdata[64];	//data for nisreq
	int errval;
	int retryscore=100;	//successes increase this up to 100; failures decrease it.
	uint8_t hackbuf[70];
	int extra;	//extra bytes to purge
	uint32_t addr, nextaddr, maxaddr;
	unsigned long chrono;

	nextaddr = start;
	maxaddr = start + len - 1;

	if (!outf) return CMD_FAILED;

	nisreq.data=txdata;	//super very essential !
	chrono = diag_os_getms();
	while (retryscore >0) {

		unsigned int linecur=0;	//count from 0 to 11 (12 addresses per request)

		int txi;	//index into txbuf for constructing request


		printf("Starting dump from 0x%08X to 0x%08X.\n", nextaddr, maxaddr);

		txdata[0]=0xAC;
		txdata[1]=0x81;
		nisreq.len = 2;	//AC 81 : 2 bytes so far
		txi=2;
		linecur = 0;

		for (addr=nextaddr; addr <= maxaddr; addr++) {
			txdata[txi++]= 0x83;		//field type
			txdata[txi++]= (uint8_t) (addr >> 24) & 0xFF;
			txdata[txi++]= (uint8_t) (addr >> 16) & 0xFF;
			txdata[txi++]= (uint8_t) (addr >> 8) & 0xFF;
			txdata[txi++]= (uint8_t) (addr & 0xFF);
			nisreq.len += 5;
			linecur += 1;

			//request 12 addresses at a time, or whatever's left at the end
			if ((linecur != 0x0c) && (addr != maxaddr))
				continue;

			printf("\n%08X: ", nextaddr);

			int i, rqok;
			//send the request "properly"

			rqok = diag_l2_send(global_l2_conn, &nisreq);
			if (rqok) {
				printf("\nhack mode : bad l2_send\n");
				retryscore -= 25;
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				break;	//out of for()
			}

			rqok=0;	//default to fail

			//and get a response; we already know the max expected length:
			// 0xEC 0x81 + 2 (short hdr) or +4 (full hdr).
			// we'll request just 4 bytes so we return very fast;
			// We should find 0xEC if it's in there no matter what kind of header.
			// We'll "purge" the next bytes when we send SID 21
			errval=diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
					NULL, hackbuf, 4, 25 + NP_RX_EXTRATIMEOUT);
			if (errval == 4) {
				//try to find 0xEC in the first bytes:
				for (i=0; i<=3 && i<errval; i++) {
					if (hackbuf[i] == 0xEC) {
						rqok=1;
						break;
					}
				}
			}

			if (!rqok) {
				printf("\nhack mode : bad AC response %02X %02X\n", hackbuf[0], hackbuf[1]);
				retryscore -= 25;
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				break;	//out of for()
			}
			//Here, we're guaranteed to have found 0xEC in the first 4 bytes we got. But we may
			//need to "purge" some extra bytes on the next read
			// hdr0 (hdr1) (hdr2) 0xEC 0x81 ck
			//
			extra = (3 + i - errval);	//bytes to purge. I think the formula is ok
			extra = (extra < 0) ? 0: extra;	//make sure >=0

			//Here, we sent a AC 81 83 ... 83... request that was accepted.
			//We need to send 21 81 04 01 to get the data now
			txdata[0]=0x21;
			txdata[1]=0x81;
			txdata[2]=0x04;
			txdata[3]=0x01;
			nisreq.len=4;

			rqok=0;	//default to fail
			//send the request "properly"
			if (diag_l2_send(global_l2_conn, &nisreq)==0) {
				//and get a response; we already know the max expected length:
				//61 81 [2+linecur] + max 4 (header+cks) = 8+linecur
				//but depending on the previous message there may be extra
				//bytes still in buffer; we already calculated how many.
				//By requesting (extra) + 4 with a short timeout, we'll return
				//here very quickly and we're certain to "catch" 0x61.
				errval=diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
						NULL, hackbuf, extra + 4, 25 + NP_RX_EXTRATIMEOUT);
				if (errval != extra+4) {
					retryscore -=25;
					diag_os_millisleep(300);
					(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
					break;	//out of for ()
				}
				//try to find 0x61 in the first bytes:
				for (i=0; i<errval; i++) {
						if (hackbuf[i] == 0x61) {
							rqok=1;
							break;
						}
				}
				//we now know where the real data starts so we can request the
				//exact number of bytes remaining. Now, (errval - i) is the number
				//of packet bytes already read including 0x61, ex.:
				//[XX XX 61 81 YY YY ..] : i=2 and errval =5 means we have (5-2)=3 bytes
				// of packet data (61 81 YY)
				// Total we need (2 + linecur) packet bytes + 1 cksum
				// So we need to read (2+linecur+1) - (errval-i) bytes...
				// Plus : we need to dump those at the end of what we already got !
				extra = (3 + linecur) - (errval - i);
				if (extra<0) {
					printf("\nhack mode : problem ! extra=%d\n",extra);
					extra=0;
				} else {
					errval=diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
						NULL, &hackbuf[errval], extra, 25 + NP_RX_EXTRATIMEOUT);
				}

				if (errval != extra)	//this should always fit...
					rqok=0;

				if (!rqok) {
					//either negative response or not enough data !
					printf("\nhack mode : bad 61 response %02X %02X, i=%02X extra=%02X ev=%02X\n",
							hackbuf[i], hackbuf[i+1], i, extra, errval);
					diag_os_millisleep(300);
					(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
					retryscore -= 25;
					break;	//out of for ()
				}
				//and verify checksum. [i] points to 0x61;
				if (hackbuf[i+2+linecur] != diag_cks1(&hackbuf[i-1], 3+linecur)) {
					//this checksum will not work with long headers...
					printf("\nhack mode : bad 61 CS ! got %02X\n", hackbuf[i+2+linecur]);
					diag_data_dump(stdout, &hackbuf[i], linecur+3);
					diag_os_millisleep(300);
					(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
					retryscore -=20;
					break;	//out of for ()
				}

			}	//if l2_send OK

				//We can now dump this to the file...
			if (fwrite(&(hackbuf[i+2]), 1, linecur, outf) != linecur) {
				printf("Error writing file!\n");
				retryscore -= 101;	//fatal, sir
				break;	//out of for ()
			}
			diag_data_dump(stdout, &hackbuf[i+2], linecur);

			nextaddr += linecur;	//if we crash, we can resume starting at nextaddr
			linecur=0;
			//success: allow us more errors
			retryscore = (retryscore > 95)? 100:(retryscore+5) ;

			//and reset tx template + sub-counters
			txdata[0]=0xAc;
			txdata[1]=0x81;
			nisreq.len=2;
			txi=2;

		}	//for
		if (addr <= maxaddr) {
			//the for loop didn't complete;
			//(if succesful, addr == maxaddr+1 !!)
			printf("\nRetry score: %d\n", retryscore);
		} else {
			printf("\nFinished! ~%lu Bps\n", 1000*(maxaddr - start)/(diag_os_getms() - chrono));
			break;	//leave while()
		}
	}	//while retryscore>0

	fclose(outf);

	if (retryscore <= 0) {
			//there was an error inside and no retries left
		printf("Too many errors, no more retries @ addr=%08X.\n", start);
		return CMD_FAILED;
	}
	return CMD_OK;
}

static int np_6_7(UNUSED(int argc), UNUSED(char **argv), int keyalg, uint32_t scode) {
	//np 6 & 7 : attempt a SecurityAccess (SID 27), using selected algo.
	//np 6: genkey2 (KLINE_AT)
	//np 7: genkey1 (NPT_DDL2) + scode
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x27;
	txdata[1]=0x01;	//RequestSeed
	nisreq.len=2;
	nisreq.data=txdata;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return CMD_FAILED;
	if ((rxmsg->len < 6) || (rxmsg->data[0] != 0x67)) {
		printf("got bad 27 01 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return CMD_FAILED;
	}
	printf("Trying SID 27, got seed: ");
	diag_data_dump(stdout, &rxmsg->data[2], 4);

	txdata[0]=0x27;
	txdata[1]=0x02;	//SendKey
	switch (keyalg) {
	case 1:
		genkey1(&rxmsg->data[2], scode, &txdata[2]);	//write key to txdata buffer
		printf("; using NPT_DDL algo (scode=0x%0X), ", scode);
		break;
	case 2:
	default:
		genkey2(&rxmsg->data[2], &txdata[2]);	//write key to txdata buffer
		printf("; using KLINE_AT algo, ");
		break;
	}
	diag_freemsg(rxmsg);

	printf("to send key ");
	diag_data_dump(stdout, &txdata[2], 4);
	printf("\n");

	nisreq.len=6; //27 02 K K K K
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return CMD_FAILED;
	if (rxmsg->data[0] != 0x67) {
		printf("got bad 27 02 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return CMD_FAILED;
	}
	printf("SUXXESS !!\n");

	diag_freemsg(rxmsg);
	return CMD_OK;
}

/** Dump memory to a file (direct binary copy)
 * @param froot: optional; prefix to auto-generated output filename
 * @param hackmode: if set, use shortcut method for reads (bypass L2_recv)
 * @return CMD_OK or CMD_FAILED
 */
static int dumpmem(const char *froot, uint32_t start, uint32_t len, bool hackmode) {
	// try with P3min = 5ms rather than 55ms; this should
	// save ~8ms per byte overall.
#define DUMPFILESZ 30
	FILE *romdump;
	char romfile[DUMPFILESZ+1]="";
	char * openflags;
	uint32_t nextaddr;	//start addr
	uint32_t maxaddr;	//end addr
	struct diag_l2_14230 * dlproto;	// for bypassing headers

	nextaddr = start;
	maxaddr = start + len - 1;

	global_l2_conn->diag_l2_p4min=0;	//0 interbyte spacing
	global_l2_conn->diag_l2_p3min=5;	//5ms before new requests

	snprintf(romfile, DUMPFILESZ, "%s_%X-%X.bin", froot, start, start+len - 1);

	//this allows download resuming if starting address was >0
	openflags = (start>0)? "ab":"wb";

	//Create / append to "rom-[ECUID].bin"
	if ((romdump = fopen(romfile, openflags))==NULL) {
		printf("Cannot open %s !\n", romfile);
		return CMD_FAILED;
	}

	dlproto=(struct diag_l2_14230 *)global_l2_conn->diag_l2_proto_data;
	if (dlproto->modeflags & ISO14230_SHORTHDR) {
		printf("Using short headers.\n");
		dlproto->modeflags &= ~ISO14230_LONGHDR;	//deactivate long headers
	} else {
		printf("cannot use hackmode; short headers not supported ! Have you \"set addrtype phys\" ?\n"
				"Using slow np_4 method as fallback.\n");
		hackmode=0;	//won't work without short headers
	}

	if (!hackmode) {
		int rv=np_4(romdump, nextaddr, maxaddr - nextaddr + 1);
		fclose(romdump);
		if (rv != CMD_OK) {
			printf("Errors occured, dump may be incomplete.\n");
			return CMD_FAILED;
		}
		return CMD_OK;
	}
	int rv = np_5(romdump, nextaddr, maxaddr - nextaddr +1);
	fclose(romdump);
	if (rv != CMD_OK) {
		printf("Errors occured, dump may be incomplete.\n");
		return CMD_FAILED;
	}
	return CMD_OK;

}

/** Read bytes from memory
 * copies <len> bytes from <raddr> to dest,
 * using SID AC and std L2_request mechanism.
 * Uses global conn, assumes global state is OK
 * @return num of bytes read
 */
uint32_t read_ac(uint8_t *dest, uint32_t raddr, uint32_t len) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;
	uint32_t addr;
	uint32_t sent;	//count
	uint32_t goodbytes;

	if (!dest || (len==0)) return 0;

	unsigned int linecur;	//count from 0 to 11 (12 addresses per request)

	addr = raddr;

	int txi;	//index into txbuf for constructing request

	txdata[0]=0xAC;
	txdata[1]=0x81;
	nisreq.len = 2;	//AC 81 : 2 bytes so far
	nisreq.data=txdata;
	txi=2;
	linecur = 0;
	goodbytes = 0;
	for (sent=0; sent < len; addr++) {
		txdata[txi++]= 0x83;		//field type
		txdata[txi++]= (uint8_t) (addr >> 24) & 0xFF;
		txdata[txi++]= (uint8_t) (addr >> 16) & 0xFF;
		txdata[txi++]= (uint8_t) (addr >> 8) & 0xFF;
		txdata[txi++]= (uint8_t) (addr & 0xFF);
		nisreq.len += 5;
		linecur += 1;
		sent++;
		//request 12 addresses at a time, or whatever's left at the end
		if ((linecur != 0x0c) && (sent != len))
			continue;

		rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL) {
			printf("\nError: no resp to rqst AC @ %08X, err=%d\n", addr, errval);
			break;	//leave for loop
		}
		if ((rxmsg->data[0] != 0xEC) || (rxmsg->len != 2) ||
				(rxmsg->fmt & DIAG_FMT_BADCS)) {
			printf("\nFatal : bad AC resp at addr=0x%X: %02X, len=%u\n", addr,
				rxmsg->data[0], rxmsg->len);
			diag_freemsg(rxmsg);
			break;
		}
		diag_freemsg(rxmsg);

		//Here, we sent a AC 81 83 ... 83... request that was accepted.
		//We need to send 21 81 04 01 to get the data now
		txdata[0]=0x21;
		txdata[1]=0x81;
		txdata[2]=0x04;
		txdata[3]=0x01;
		nisreq.len=4;

		rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL) {
			printf("\nFatal : did not get response at address %08X, err=%d\n", addr, errval);
			break;	//leave for loop
		}
		if ((rxmsg->data[0] != 0x61) || (rxmsg->len != (2+linecur)) ||
				(rxmsg->fmt & DIAG_FMT_BADCS)) {
			printf("\nFatal : error at addr=0x%X: %02X, len=%u\n", addr,
				rxmsg->data[0], rxmsg->len);
			diag_freemsg(rxmsg);
			break;
		}
		//Now we got the reply to SID 21 : 61 81 x x x ...
		memcpy(dest, &(rxmsg->data[2]), linecur);
		dest = &dest[linecur];
		goodbytes = sent;

		if (rxmsg)
			diag_freemsg(rxmsg);

		linecur=0;

		//and reset tx template + sub-counters
		txdata[0]=0xAc;
		txdata[1]=0x81;
		nisreq.len=2;
		txi=2;

	}	//for

	return goodbytes;
}

//np 8 : (WIP) watch 4 bytes @ specified addr, using SID AC.
//"np 8 <addr>"
int np_8(int argc, char **argv) {
	uint32_t addr;
	uint32_t len;
	uint8_t wbuf[4];

	if (argc != 3) {
		printf("usage: np 8 <addr>: watch 4 bytes @ <addr>\n");
		return CMD_USAGE;
	}
	addr = (uint32_t) htoi(argv[2]);
	printf("\nMonitoring 0x%0X; press Enter to interrupt.\n", addr);
	(void) diag_os_ipending();	//must be done outside the loop first
	while ( !diag_os_ipending()) {

		len = read_ac(wbuf, addr, 4);
		if (len != 4) {
			printf("? got %u bytes\n", len);
			break;
		}
		printf("\r0x%0X: %02X %02X %02X %02X", addr, wbuf[0], wbuf[1], wbuf[2], wbuf[3]);
		fflush(stdout);
	}
	printf("\n");
	return CMD_OK;
}

/** encrypt buffer in-place
 * @param len (count in bytes) is trimmed to align on 4-byte boundary, i.e. len=7 => len =4
 *
 * @return 16-bit checksum of buffer prior to encryption
 *
 */
uint16_t encrypt_buf(uint8_t *buf, uint32_t len, uint32_t key) {
	uint16_t cks;
	if (!buf || !len) return 0;

	len &= ~3;
	cks = 0;
	for (; len > 0; len -= 4) {
		uint8_t tempbuf[4];
		memcpy(tempbuf, buf, 4);
		cks += tempbuf[0];
		cks += tempbuf[1];
		cks += tempbuf[2];
		cks += tempbuf[3];
		genkey1(tempbuf, key, buf);
		buf += 4;
	}
	return cks;
}

// hax, get file length but restore position. taken from nislib
static long flen(FILE *hf) {
	long siz;
	long orig;

	if (!hf) return 0;
	orig = ftell(hf);
	if (orig < 0) return 0;

	if (fseek(hf, 0, SEEK_END)) return 0;

	siz = ftell(hf);
	if (siz < 0) siz=0;
	if (fseek(hf, orig, SEEK_SET)) return 0;
	return siz;
}

/** do SID 34 80 transaction, ret 0 if ok
 *
 * Assumes everything is ok (conn state, etc)
 */
int sid3480(void) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x34;
	txdata[1]=0x80;
	nisreq.len=2;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0x74) {
		printf("got bad 34 80 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);
	return 0;
}

/* transfer payload from *buf
 * len must be multiple of 32
 * Caller must have encrypted the payload
 * ret 0 if ok
 */
int sid36(uint8_t *buf, uint32_t len) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	int errval;
	uint16_t blockno;
	uint16_t maxblocks;

	len &= ~0x1F;
	if (!buf || !len) return -1;

	blockno = 0;
	maxblocks = (len / 32) - 1;

	txdata[0]=0x36;
	//txdata[1] and [2] is the 16bit block #
	txdata[3] = 0x20;		//block length; ignored by ECU
	nisreq.data=txdata;
	nisreq.len= 4 + 32;

	for (; len > 0; len -= 32, blockno += 1) {
		uint8_t rxbuf[10];	//can't remember what the actual sid 36 response looks like

		txdata[1] = blockno >> 8;
		txdata[2] = blockno & 0xFF;

		memcpy(&txdata[4], buf, 32);
		buf += 32;

		//rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		errval = diag_l2_send(global_l2_conn, &nisreq);
		if (errval) {
			printf("l2_send error!\n");
			return -1;
		}

		/* this will always time out since the response is probably always 5 bytes */
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf, sizeof(rxbuf), 25);
		if (errval <= 3) {
			printf("no response @ blockno %X\n", (unsigned) blockno);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}

		if (rxbuf[0] & 0x80) {
			//with address : response looks like "<len | 0x80> <src> <dest> <resp>"
			rxbuf[0] = rxbuf[3];
		} else {
			//no address : "<len> <resp> <cks>"
			rxbuf[0] = rxbuf[1];
		}
		if (rxbuf[0] != 0x76) {
			printf("got bad 36 response : ");
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
			return -1;
		}
		printf("\rSID36 block 0x%04X/0x%04X done",
				(unsigned) blockno, (unsigned) maxblocks);
	}
	printf("\n");
	fflush(stdout);
	return 0;
}

//send SID 37 transferexit request, ret 0 if ok
int sid37(uint16_t cks) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x37;
	txdata[1]=cks >> 8;
	txdata[2]=cks & 0xFF;
	nisreq.len=3;
	nisreq.data=txdata;

	printf("sid37: sending ");
	diag_data_dump(stdout, txdata, 3);
	printf("\n");

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0x77) {
		printf("got bad 37 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);
	return 0;
}

/* RAMjump, takes care of SIDs BF 00 + BF 01
 * ret 0 if ok
 */
int sidBF(void) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0xBF;
	txdata[1]=0;
	nisreq.len=2;
	nisreq.data=txdata;

	/* BF 00 : RAMjumpCheck */
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0xFF) {
		printf("got bad BF 00 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);

	/* BF 01 : RAMjumpCheck */
	txdata[1] = 1;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0xFF) {
		printf("got bad BF 01 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);
	return 0;
}


/* Does a complete SID 27 + 34 + 36 + BF sequence to run the given kernel payload file.
 * Pads the input payload up to multiple of 32 bytes to make SID36 happy
 */
int np_9(int argc, char **argv) {
	uint32_t sid27key;
	uint32_t sid36key;
	uint32_t file_len;
	uint32_t pl_len;
	uint16_t old_p3;
	uint16_t old_p4;
	FILE *fpl;
	uint8_t *pl_encr;	//encrypted payload buffer

	if (argc != 5) {
		printf("Transfer + run payload. Usage: np 9 <payload file> <sid27key> <sid36key>\n");
		return CMD_USAGE;
	}

	sid27key = (uint32_t) htoi(argv[3]);
	sid36key = (uint32_t) htoi(argv[4]);

	if ((fpl = fopen(argv[2], "rb"))==NULL) {
		printf("Cannot open %s !\n", argv[2]);
		return CMD_FAILED;
	}

	file_len = (uint32_t) flen(fpl);
	/* pad up to next multiple of 32 */
	pl_len = (file_len + 31) & ~31;

	if (diag_malloc(&pl_encr, pl_len)) {
		printf("malloc prob\n");
		return CMD_FAILED;
	}

	if (fread(pl_encr, 1, file_len, fpl) != file_len) {
		printf("fread prob, file_len=%u\n", file_len);
		free(pl_encr);
		fclose(fpl);
		return CMD_FAILED;
	}

	if (file_len != pl_len) {
		printf("Using %u byte payload, padding with garbage to %u (0x0%X) bytes.\n", file_len, pl_len, pl_len);
	} else {
		printf("Using %u (0x0%X) byte payload", file_len, file_len);
	}

	old_p4 = global_l2_conn->diag_l2_p4min;
	global_l2_conn->diag_l2_p4min = 0;	//0 interbyte spacing
	old_p3 = global_l2_conn->diag_l2_p3min;
	global_l2_conn->diag_l2_p3min = 5;	//0 delay before new req

	/* re-use NP 7 to get the SID27 done */
	if (np_6_7(1, argv, 1, sid27key) != CMD_OK) {
		printf("sid27 problem\n");
		goto badexit;
	}

	/* SID 34 80 : */
	if (sid3480()) {
		printf("sid 34 80 problem\n");
		goto badexit;
	}
	printf("SID 34 80 done.\n");

	/* encrypt + send payload with SID 36 */
	uint16_t cks = 0;
	cks = encrypt_buf(pl_encr, (uint32_t) pl_len, sid36key);

	if (sid36(pl_encr, (uint32_t) pl_len)) {
		printf("sid 36 problem\n");
		goto badexit;
	}
	printf("SID 36 done.\n");

	/* SID 37 TransferExit */
	if (sid37(cks)) {
		printf("sid 37 problem\n");
		goto badexit;
	}
	printf("SID 37 done.\n");

	/* shit gets real here : RAMjump ! */
	if (sidBF()) {
		printf("RAMjump problem\n");
		goto badexit;
	}

	global_l2_conn->diag_l2_p4min = old_p4;
	global_l2_conn->diag_l2_p3min = old_p3;
	free(pl_encr);
	fclose(fpl);

	printf("SID BF done.\nECU now running from RAM ! Disabling periodic keepalive;\n");

	if (!npkern_init()) {
		printf("You may proceed with kernel-specific commands; speed has been changed to %u.\n", NPK_SPEED);
	} else {
		printf("Problem starting kernel; try to disconnect + set speed + connect again.\n");
	}

	return CMD_OK;

badexit:
	global_l2_conn->diag_l2_p4min = old_p4;
	global_l2_conn->diag_l2_p3min = old_p3;
	free(pl_encr);
	fclose(fpl);
	return CMD_FAILED;
}


/** set speed + do startcomms, sabotage L2 modeflags for short headers etc.
 * Also disables keepalive
 * ret 0 if ok
 */
static int npkern_init(void) {
	struct diag_serial_settings set;
	struct diag_l2_14230 *dlproto;
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	nisreq.data=txdata;

	/* Assume kernel is freshly booted : disable keepalive and setspeed */
	global_l2_conn->tinterval = -1;

	set.speed = NPK_SPEED;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	errval=diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_SETSPEED, (void *) &set);
	if (errval) {
		printf("npk_init: could not setspeed\n");
		return -1;
	}
	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);

	global_l2_conn->diag_l2_p4min = 0;	//0 interbyte spacing
	global_l2_conn->diag_l2_p3min = 5;	//5ms before sending new requests
	dlproto = (struct diag_l2_14230 *)global_l2_conn->diag_l2_proto_data;
	dlproto->modeflags = ISO14230_SHORTHDR | ISO14230_LENBYTE | ISO14230_FMTLEN;

	/* StartComm */
	txdata[0] = 0x81;
	nisreq.len = 1;
	rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (!rxmsg) {
		printf("npk_init: startcomm failed : %d\n", errval);
		return -1;
	}
	if (rxmsg->data[0] != 0xC1) {
		printf("npk_init: got bad startcomm response\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);

	return 0;
}


/** npkernel SID 23 ReadMemoryByAddress
 * Supports addresses in two ranges :
 * [0 - 0x7F FFFF]	(bottom 8MB)
 * [0xFF80 0000 - 0xFFFF FFFF] (top 8MB)
 * that's 24 bits of addressing space of course.
 *
 * Assumes init was done before
 * ret 0 if ok
 */
static int npk_RMBA(uint8_t *dest, uint32_t addr, uint32_t len) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	nisreq.data=txdata;

	bool start_ROM = (addr < 0x800000);
	bool not_ROM = !start_ROM;
	bool start_RAM = (addr >= 0xFF800000);
	bool not_RAM = !start_RAM;

	if (((start_ROM) && ((addr + len) > 0x800000)) ||
		(not_ROM && not_RAM)) {
		printf("npk RMBA addr out of bounds\n");
		return -1;
	}

	txdata[0] = 0x23;
	nisreq.len = 5;

	while (len) {
		uint32_t curlen;
		txdata[1] = addr >> 16;
		txdata[2] = addr >> 8;
		txdata[3] = addr >> 0;
		curlen = len;
		if (curlen > 251) curlen = 251;	//SID 23 limitation
		txdata[4] = (uint8_t) curlen;

		rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (!rxmsg) {
			printf("npk sid23 failed : %d\n", errval);
			return -1;
		}
		if ((rxmsg->data[0] != 0x63) || (rxmsg->len != curlen + 4)) {
			printf("got bad / incomplete SID23 response:\n");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
			return -1;
		}
		memcpy(dest, &rxmsg->data[1], curlen);
		diag_freemsg(rxmsg);
		len -= curlen;
		dest += curlen;
		addr += curlen;
	}
	return 0;

}


/** receive a bunch of dumpblocks (caller already send the dump request).
 * doesn't write the first "skip_start" bytes
 * ret 0 if ok
 */
static int npk_rxrawdump(uint8_t *dest, uint32_t skip_start, uint32_t numblocks) {
	uint8_t rxbuf[260];
	int errval;
	uint32_t bi;

	for(bi = 0; bi < numblocks; bi ++) {
		//loop for every 32-byte response

		/* grab header. Assumes we only get "FMT PRC <data> cks" replies */
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf, 3 + 32, 25 + NP_RX_EXTRATIMEOUT);
		if (errval < 0) {
			printf("dl1recv err\n");
			goto badexit;
		}
		uint8_t cks = diag_cks1(rxbuf, 2 + 32);
		if ((errval != 35) || (rxbuf[0] != 0x21) || (rxbuf[1] != 0xFD) || (cks != rxbuf[34])) {
			printf("no / incomplete / bad response\n");
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
			goto badexit;
		}
		uint32_t datapos = 2;	//position inside rxbuf
		if (skip_start) {
			datapos += skip_start;	//because start addr wasn't aligned
			skip_start = 0;
		}

		uint32_t cplen = 34 - datapos;
		memcpy(dest, &rxbuf[datapos], cplen);
		dest += cplen;
	}	//for
	return 0;

badexit:
	return -1;
}

/* npkern-based fastdump (EEPROM / ROM / RAM) */
/* kernel must be running first (np 9)*/
static int np_10(int argc, char **argv) {
	uint32_t start, len;
	uint16_t old_p3;
	FILE *fpl;

	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	int errval;

	bool eep = 0;
	bool ram = 0;

	nisreq.data=txdata;

	if (argc < 5) {
		printf("npk-fastdump. Usage: np 10 <output file> <start> <len> [eep]\n"
				"ex.: \"np 10 eeprom_dump.bin 0 512 eep\"\n"
				"ex.: \"np 10 romdump_ivt.bin 0 0x400\"\n");
		return CMD_USAGE;
	}

	start = (uint32_t) htoi(argv[3]);
	len = (uint32_t) htoi(argv[4]);

	if (start > 0xFF800000) ram = 1;

	if (argc == 6) {
		if (strcmp("eep", argv[5]) == 0) eep = 1;
	}

	if (ram && eep) {
		printf("bad args\n");
		return CMD_FAILED;
	}

	if ((fpl = fopen(argv[2], "wb"))==NULL) {
		printf("Cannot open %s !\n", argv[2]);
		return CMD_FAILED;
	}
	old_p3 = global_l2_conn->diag_l2_p3min;
	global_l2_conn->diag_l2_p3min = 0;	//0 delay before sending new request

	if (npkern_init()) {
		printf("npk init failed\n");
		goto badexit;
	}

	uint32_t skip_start = start & (32 - 1);	//if unaligned, we'll be receiving this many extra bytes
	uint32_t iter_addr = start - skip_start;
	uint32_t willget = (skip_start + len + 31) & ~(32 - 1);
	uint32_t len_done = 0;	//total data written to file

	txdata[0] = 0xBD;
	txdata[1] = eep? 0 : 1;
#define NP10_MAXBLKS	8	//# of blocks to request per loop. Too high might flood us
	nisreq.len = 6;

	unsigned t0 = diag_os_getms();

	while (willget) {
		uint8_t buf[NP10_MAXBLKS * 32];
		uint32_t numblocks;

		unsigned curspeed, tleft;
		unsigned long chrono;

		chrono = diag_os_getms() - t0;
		if (!chrono) chrono += 1;
		curspeed = 1000 * len_done / chrono;	//avg B/s
		if (!curspeed) curspeed += 1;
		tleft = willget / curspeed;	//s
		printf("\rnpk dump @ 0x%08X, %5u B/s, %5u s remaining", iter_addr, curspeed, tleft);

		numblocks = willget / 32;

		if (numblocks > NP10_MAXBLKS) numblocks = NP10_MAXBLKS;	//ceil

		txdata[2] = numblocks >> 8;
		txdata[3] = numblocks >> 0;

		uint32_t curblock = (iter_addr / 32);
		txdata[4] = curblock >> 8;
		txdata[5] = curblock >> 0;

		if (ram) {
			errval = npk_RMBA(buf, iter_addr + skip_start, (numblocks * 32) - skip_start);
			if (errval) {
				printf("RMBA error!\n");
				goto badexit;
			}
		} else {
			errval = diag_l2_send(global_l2_conn, &nisreq);
			if (errval) {
				printf("l2_send error!\n");
				goto badexit;
			}
			if (npk_rxrawdump(buf, skip_start, numblocks)) {
				printf("rxrawdump failed\n");
				goto badexit;
			}
		}

		/* don't count skipped first bytes */
		uint32_t cplen = (numblocks * 32) - skip_start;	//this is the actual # of valid bytes in buf[]
		skip_start = 0;

		/* and drop extra bytes at the end */
		uint32_t extrabytes = (cplen + len_done);	//hypothetical new length
		if (extrabytes > len) {
			cplen -= (extrabytes - len);
			//thus, (len_done + cplen) will not exceed len
		}
		uint32_t done = fwrite(buf, 1, cplen, fpl);
		if (done != cplen) {
			printf("fwrite error\n");
			goto badexit;
		}

		/* increment addr, len, etc */
		len_done += cplen;
		iter_addr += (numblocks * 32);
		willget -= (numblocks * 32);

	}	//while
	printf("\n");

	fclose(fpl);
	global_l2_conn->diag_l2_p3min = old_p3;
	return CMD_OK;

badexit:
	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	fclose(fpl);
	global_l2_conn->diag_l2_p3min = old_p3;
	return CMD_FAILED;
}


/* npkernel : reset ECU */
static int np_11(UNUSED(int argc), UNUSED(char **argv)) {
	uint8_t txdata[1];
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x11;
	nisreq.len=1;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) return CMD_FAILED;

	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	return CMD_OK;
}


/* special checksum for reflash blocks:
 * "one's complement" checksum; if adding causes a carry, add 1 to sum. Slightly better than simple 8bit sum
 */
static uint8_t cks_add8(uint8_t *data, unsigned len) {
	uint16_t sum = 0;
	for (; len; len--, data++) {
		sum += *data;
		if (sum & 0x100) sum += 1;
		sum = (uint8_t) sum;
	}
	return sum;
}


/* ret 0 if ok. For use by np_12,
 * assumes parameters have been validated,
 * and appropriate block has been erased
 */

static int npk_raw_flashblock(uint8_t *src, uint32_t start, uint32_t len) {

	/* program 128-byte chunks */
	uint32_t remain = len;

	uint8_t txdata[134];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	int errval;
	nisreq.data = txdata;

	unsigned long t0, chrono;

	if ((len & (128 - 1)) ||
		(start & (128 - 1))) {
		printf("error: misaligned start / length ! \n");
		return -1;
	}

	txdata[0]=0xBC;
	txdata[1]=0x02;
	nisreq.len = 134;	//2 (header) + 3 (addr) + 128 (payload) + 1 (extra CRC)

	t0 = diag_os_getms();


	while (remain) {
		uint8_t rxbuf[10];
		unsigned curspeed, tleft;

		chrono = diag_os_getms() - t0;
		if (!chrono) chrono += 1;
		curspeed = 1000 * (len - remain) / chrono;	//avg B/s
		if (!curspeed) curspeed += 1;
		tleft = remain / curspeed;	//s
		if (tleft > 9999) tleft = 9999;

		printf("\rwriting chunk @ 0x%06X (%3u %%, %5u B/s, ~ %4u s remaining)", start, (unsigned) 100 * (len - remain) / len,
				curspeed, tleft);

		txdata[2] = start >> 16;
		txdata[3] = start >> 8;
		txdata[4] = start >> 0;
		memcpy(&txdata[5], src, 128);
		txdata[133] = cks_add8(&txdata[2], 131);

		errval = diag_l2_send(global_l2_conn, &nisreq);
		if (errval) {
			printf("l2_send error!\n");
			return -1;
		}

		/* expect exactly 3 bytes, but with generous timeout */
		//rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf, 3, 300);
		if (errval <= 1) {
			printf("\n\tProblem: no response @ %X\n", (unsigned) start);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}
		if (errval < 3) {
			printf("\n\tProblem: incomplete response @ %X\n", (unsigned) start);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
			return -1;
		}

		if (rxbuf[1] != 0xFC) {
			//maybe negative response, if so, get the remaining packet
			printf("\n\tProblem: bad response @ %X\n", (unsigned) start);

			int needed = 1 + rxbuf[0] - errval;
			if (needed > 0) {
				errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, &rxbuf[errval], needed, 300);
			}
			if (errval < 0) errval = 0;	//floor
			diag_data_dump(stdout, rxbuf, rxbuf[0] + errval);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}

		remain -= 128;
		start += 128;
		src += 128;

	}	//while len
	printf("\nWrite complete.\n");

	return 0;
}

#include "flashdefs.h"
/* reflash a given block !
 */
static int np_12(int argc, char **argv) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	int errval;
	struct diag_msg *rxmsg;

	FILE *fpl;
	uint8_t *newdata;	//file will be copied to this

	unsigned blockno;
	uint32_t start;
	uint32_t len;

	bool for_real = 0;	//if set, enables real flash erase + write

	nisreq.data=txdata;

	if (argc <= 3) {
		printf("npk-blockwrite. Usage: np 12 <data.bin> <blockno> [Y]\n"
				"If 'Y' is absent, will run in \"practice\" mode (no erase / write).\n"
				"ex.: \"np 12 blk_0xE0000-0xFFFFF.bin 15 Y\"\n");
		return CMD_FAILED;
	}

	blockno = (unsigned) htoi(argv[3]);

	if (blockno >= ARRAY_SIZE(fblocks_7058)) {
		printf("block # out of range !\n");
		return CMD_FAILED;
	}

	start = fblocks_7058[blockno].start;
	len = fblocks_7058[blockno].len;

	if ((fpl = fopen(argv[2], "rb"))==NULL) {
		printf("Cannot open %s !\n", argv[2]);
		return CMD_FAILED;
	}

	if ((uint32_t) flen(fpl) != len) {
		printf("error : data file doesn't match expected block length %uk\n", (unsigned) len / 1024);
		goto badexit_nofree;
	}

	if (argc == 5) {
		if (argv[4][0] == 'Y') printf("*** FLASH WILL BE MODIFIED ***\n");
		for_real = 1;
	} else {
		printf("*** Running in practice mode, flash will not be modified ***\n");
	}

	if (diag_malloc(&newdata, len)) {
		printf("malloc prob\n");
		goto badexit_nofree;
	}

	if (fread(newdata, 1, len, fpl) != len) {
		printf("fread prob !?\n");
		goto badexit;
	}

	if (npkern_init()) {
		printf("npk init failed\n");
		goto badexit;
	}

	/* 1- requestdownload */
	txdata[0]=0x34;
	nisreq.len = 1;
	rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		goto badexit;
	if (rxmsg->data[0] != 0x74) {
		printf("got bad RequestDownload response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		goto badexit;
	}

	/* 2- Unprotect maybe. TODO : use SID defines here and after */
	if (for_real) {
		(void) diag_os_ipending();	//must be done outside the loop first
		printf("*** Last chance : operation will be safely aborted in 3 seconds. ***\n"
				"*** Press ENTER to MODIFY FLASH ***\n");
		diag_os_millisleep(3000);
		if (diag_os_ipending()) {
			printf("Proceeding with flash process.\n");
		} else {
			printf("Operation aborted; flash was not modified.\n");
			goto badexit;
		}

		txdata[0]=0xBC;
		txdata[1]=0x55;
		txdata[2]=0xaa;
		nisreq.len = 3;
		rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL)
			goto badexit_reprotect;
		if (rxmsg->data[0] != 0xFC) {
			printf("got bad Unprotect response : ");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
			goto badexit_reprotect;
		}
		printf("Entered flashing_enabled (unprotected) mode\n");
	}

	/* 3- erase block */
	printf("Erasing block %u (0x%06X-0x%06X)...\n",
			blockno, (unsigned) start, (unsigned) start + len - 1);
	txdata[0] = 0xBC;
	txdata[1] = 0x01;
	txdata[2] = blockno;
	nisreq.len = 3;
	/* Problem : erasing can take a lot more than the default P2max for iso14230 */
	uint16_t old_p2max = global_l2_conn->diag_l2_p2max;
	global_l2_conn->diag_l2_p2max = 1200;
	rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
	global_l2_conn->diag_l2_p2max = old_p2max;	//restore p2max; the rest should be OK
	if (rxmsg==NULL) {
		printf("no ERASE_BLOCK response?\n");
		goto badexit_reprotect;
	}
	if (rxmsg->data[0] != 0xFC) {
		printf("got bad ERASE_BLOCK response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		goto badexit_reprotect;
	}

	/* 4- write */
	errval = npk_raw_flashblock(newdata, start, len);
	if (errval) {
		printf("\nReflash error ! Do not panic, do not reset the ECU immediately. The kernel is "
				"most likely still running and receiving commands !\n");
		goto badexit_reprotect;
	}

	printf("Reflash complete; you may dump the ROM again to be extra sure\n");
	free(newdata);
	fclose(fpl);
	return CMD_OK;

badexit_reprotect:
	npkern_init();	//forces the kernel to disable write mode
badexit:
	free(newdata);
badexit_nofree:
	fclose(fpl);
	return CMD_FAILED;
}

static int cmd_diag_nisprog(int argc, char **argv) {
	unsigned testnum;
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq
	static uint8_t ECUID[7]="";
	int errval;
	int hackmode=0;	//to modify test #4's behavior
	uint32_t scode;	//for SID27

	if ((argc <=1) || (sscanf(argv[1],"%u", &testnum) != 1)) {
		printf("Bad args\n");
		return CMD_USAGE;
	}

	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_FAILED;
	}

	if (global_state == STATE_L3ADDED) {
		printf("This can't be used through L3 !\n");
		return CMD_FAILED;
	}

	nisreq.data=txdata;	//super very essential !

	switch (testnum) {
	case 0:
		//request ECUID
		txdata[0]=0x1A;
		txdata[1]=0x81;
		nisreq.len=2;
		rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL)
			return CMD_FAILED;
		if ((rxmsg->len < 7) || (rxmsg->data[0] != 0x5A)) {
			printf("got bad 1A response : ");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
			return CMD_FAILED;
		}
		memcpy(ECUID, rxmsg->data + 1, 6);	//skip 0x5A
		ECUID[6]=0;	//null-terminate ecuid string !
		printf("ECUID: %s\n", (char *) ECUID);
		diag_freemsg(rxmsg);
		break;
	case 1:
		return np_1(argc, argv);
		break;
	case 2:
		return np_2(argc, argv);
		break;
	case 3:
		//SID A4: dump the first 256-byte page,
		printf("This test has been removed.\n");
		break;
	case 5:
		//this is a "hack mode" of case 4: instead of using L2's request()
		//interface, we use L2_send and L1_recv directly; this is much faster.
		/* TODO : change syntax to np [4|5] <start> <len> ? */
		hackmode=1;
		printf("**** Activating Hackmode 5 ! ****\n\n");
	case 4:
		// ex.: "np 4 0 511" to dump from 0 to 511.
		{	//cheat : code block to allow local var decls
		uint32_t nextaddr, maxaddr;
		if (argc != 4) {
			printf("Bad args. np 4 <start> <end>\n");
			return CMD_USAGE;
		}

		maxaddr = (uint32_t) htoi(argv[3]);
		nextaddr = (uint32_t) htoi(argv[2]);

		if (nextaddr > maxaddr) {
			printf("bad args.\n");
			return CMD_FAILED;
		}

		return dumpmem((const char *)ECUID, nextaddr, maxaddr - nextaddr + 1, hackmode);
		}
		break;	//cases 4,5 : dump with AC
	case 7:
		if (argc != 3) {
			printf("SID27 test. usage: np 7 <scode>\n");
			return CMD_USAGE;
		}
		if ((sscanf(argv[2], "%x", &scode) != 1)) {
			printf("Did not understand %s\n", argv[2]);
			return CMD_USAGE;
		}
		return np_6_7(argc, argv, 1, scode);
	case 6:
		return np_6_7(argc, argv, 2, 0);
		break;	//case 6,7 (sid27)
	case 8:
		return np_8(argc, argv);
		break;
	case 9:
		return np_9(argc, argv);
		break;
	case 10:
		return np_10(argc, argv);
		break;
	case 11:
		return np_11(argc, argv);
		break;
	case 12:
		return np_12(argc, argv);
		break;
	default:
		return CMD_USAGE;
		break;
	}	//switch testnum


	return CMD_OK;
}
