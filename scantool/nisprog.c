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
//This is highly experimental, and only tested on a 2004 Sentra Spec V
//My goal is to make a "nisprog" standalone program
//that compiles against libdiag and packages these and other features, aimed
//at dumping + reflashing ROMs.
//
//As-is, this is hack quality (low) code that "trespasses" levels to go faster, skips some
//checks, and is generally not robust. But it does works on an old Win XP laptop, with a
//USB->serial converter + dumb interface, so there's hope yet.
//Uses the global l2 connection.


#include <stdbool.h>
#include "diag_os.h"

/** fwd decls **/
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
		printf("got bad response : %02X, len=%u\n", rxmsg->data[0],
				rxmsg->len);
		diag_freemsg(rxmsg);
		return CMD_OK;
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
		printf("got bad response: %02X, len=%u\n", rxmsg->data[0],
			rxmsg->len);
		diag_freemsg(rxmsg);
		return CMD_OK;
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
		}
		diag_data_dump(stderr, tbuf, res);
		if (fwrite(tbuf, 1, res, outf) != res) {
			/* partial write; bigger problem. */
			return CMD_FAILED;
		}
		chrono = diag_os_getms() - chrono;

		retryscore += (retryscore <= 95)? 5:0;

		len -= res;
		start += res;
		printf("%u bytes remaining @ ~%lu Bps = %lu s.\n", len, (1000 * res) / chrono,
				len * chrono / (res * 1000));
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
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
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

			int i, rqok=0;	//default to fail
			//send the request "properly"
			if (diag_l2_send(global_l2_conn, &nisreq)==0) {
				//and get a response; we already know the max expected length:
				// 0xEC 0x81 + 2 (short hdr) or +4 (full hdr).
				// we'll request just 4 bytes so we return very fast;
				// We should find 0xEC if it's in there no matter what kind of header.
				// We'll "purge" the next bytes when we send SID 21
				errval=diag_l1_recv(global_l2_conn->diag_link->diag_l2_dl0d,
						NULL, hackbuf, 4, 25);
				if (errval == 4) {
					//try to find 0xEC in the first bytes:
					for (i=0; i<=3 && i<errval; i++) {
						if (hackbuf[i] == 0xEC) {
							rqok=1;
							break;
						}
					}
				}
			}	// if l2_send ok
			if (!rqok) {
				printf("\nhack mode : bad AC response %02X %02X\n", hackbuf[0], hackbuf[1]);
				retryscore -= 25;
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
				errval=diag_l1_recv(global_l2_conn->diag_link->diag_l2_dl0d,
						NULL, hackbuf, extra + 4, 25);
				if (errval != extra+4) {
					retryscore -=25;
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
					errval=diag_l1_recv(global_l2_conn->diag_link->diag_l2_dl0d,
						NULL, &hackbuf[errval], extra, 25);
				}

				if (errval != extra)	//this should always fit...
					rqok=0;

				if (!rqok) {
					//either negative response or not enough data !
					printf("\nhack mode : bad 61 response %02X %02X, i=%02X extra=%02X ev=%02X\n",
							hackbuf[i], hackbuf[i+1], i, extra, errval);
					retryscore -= 25;
					break;	//out of for ()
				}
				//and verify checksum. [i] points to 0x61;
				if (hackbuf[i+2+linecur] != diag_cks1(&hackbuf[i-1], 3+linecur)) {
					//this checksum will not work with long headers...
					printf("\nhack mode : bad 61 CS ! got %02X\n", hackbuf[i+2+linecur]);
					diag_data_dump(stdout, &hackbuf[i], linecur+3);
					retryscore -=20;
					break;	//out of for ()
				}

			}	//if l2_send OK

				//We can now dump this to the file...
			if (fwrite(&(hackbuf[i+2]), 1, linecur, outf) != linecur) {
				printf("Error writing file!\n");
				retryscore -= 50;
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

			if (rxmsg)
				diag_freemsg(rxmsg);
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
		printf("got bad response : %02X, len=%u\n", rxmsg->data[0],
				rxmsg->len);
		diag_freemsg(rxmsg);
		return CMD_OK;
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
		printf("got bad response : %02X, len=%u\n", rxmsg->data[0],
				rxmsg->len);
		diag_freemsg(rxmsg);
		return CMD_OK;
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
		printf("cannot use hackmode; short headers not supported !\n");
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

	for (sent=0; sent < len; sent++, addr++) {
		txdata[txi++]= 0x83;		//field type
		txdata[txi++]= (uint8_t) (addr >> 24) & 0xFF;
		txdata[txi++]= (uint8_t) (addr >> 16) & 0xFF;
		txdata[txi++]= (uint8_t) (addr >> 8) & 0xFF;
		txdata[txi++]= (uint8_t) (addr & 0xFF);
		nisreq.len += 5;
		linecur += 1;

		//request 12 addresses at a time, or whatever's left at the end
		if ((linecur != 0x0c) && ((sent +1) != len))
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

		if (rxmsg)
			diag_freemsg(rxmsg);

		linecur=0;

		//and reset tx template + sub-counters
		txdata[0]=0xAc;
		txdata[1]=0x81;
		nisreq.len=2;
		txi=2;

	}	//for

	return sent;
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
			printf("got bad response : %02X, len=%u\n", rxmsg->data[0],
					rxmsg->len);
			diag_freemsg(rxmsg);
			return CMD_OK;
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
	default:
		return CMD_USAGE;
		break;
	}	//switch testnum


	return CMD_OK;
}
