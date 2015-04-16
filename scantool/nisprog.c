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
//Uses the global l2 connection
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


static int cmd_diag_nisprog(int argc, char **argv) {
	unsigned testnum;
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq
	static uint8_t ECUID[7]="";
	uint32_t addr;
	int errval;
	int retryscore;
	int hackmode=0;	//to modify test #4's behavior
	int keyalg=2;	//default : alg 1 (nptddl)
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

	nisreq.data=txdata;

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
		//test 2: try start diagsession, Nissan Repro style +
		// accesstimingparams (get limits + setvals)
		txdata[0]=0x10;
		txdata[1]=0x85;
		txdata[2]=0x14;
		nisreq.len=3;
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
		break;
	case 2:
		//np 2 <addr> : read 1 byte @ addr, with SID A4
		//printf("Attempting to read 1 byte @ 000000:\n");
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
		txdata[3]= (uint8_t) ((addr & 0xFF<<8) >>8);
		txdata[2]= (uint8_t) ((addr & 0xFF<<16) >>16);
		txdata[1]= (uint8_t) ((addr & 0xFF<<24) >>24);
		txdata[5]=0x04;	//TXM
		txdata[6]=0x01;	//NumResps
		nisreq.len=7;

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
		break;	//case 2 : dump 1 byte
	case 3:
		//SID A4: dump the first 256-byte page,
		printf("This test has been removed.\n");
		break;
	case 5:
		//this is a "hack mode" of case 4: instead of using L2's request()
		//interface, we use L2_send and L1_recv directly; this should allow
		//much faster speeds.
		hackmode=1;
		printf("**** Activating Hackmode 5 ! ****\n\n");
	case 4:
		//SID AC + 21 test.
		// AC 81 {83 GGGG} {83 GGGG} ... to load addresses
		// 21 81 04 01 to dump data
		// use "np 4 0 511" to dump from 0 to 511.
		// try with P3min = 5ms rather than 55ms; this should
		// save ~8ms per byte overall.

		retryscore=100;	//successes increase this up to 100; failures decrease it.

		FILE *romdump;
		char romfile[20]="rom-";
		char * openflags;
		uint32_t nextaddr;	//start addr
		uint32_t maxaddr;	//end addr
		uint8_t hackbuf[70];	//just used for "hackmode" (5)
		int extra;	//extra bytes to purge, for hackmode

		if (argc != 4) {
			printf("Bad args. np 4 <start> <end>\n");
			return CMD_USAGE;
		}

		maxaddr = (uint32_t) htoi(argv[3]);
		nextaddr = (uint32_t) htoi(argv[3]);

		//~ if ( (sscanf(argv[3], "%u", &maxaddr) != 1) ||
				//~ (sscanf(argv[2], "%u", &nextaddr) !=1)) {
			//~ printf("Did not understand %s\n", argv[2]);
			//~ return CMD_USAGE;
		//~ }

		if (nextaddr > maxaddr) {
			printf("bad args.\n");
			return CMD_FAILED;
		}

		global_l2_conn->diag_l2_p4min=0;	//0 interbyte spacing
		global_l2_conn->diag_l2_p3min=5;	//5ms before new requests

		strncat(romfile, (char *) ECUID, 6);
		strcat(romfile, ".bin");

		//this allows download resuming if starting address was >0
		openflags = (nextaddr>0)? "ab":"wb";

		//Create / append to "rom-[ECUID].bin"
		if ((romdump = fopen(romfile, openflags))==NULL) {
			printf("Cannot open %s !\n", romfile);
			return CMD_FAILED;
		}

		while (retryscore >0) {

			unsigned int linecur=0;	//count from 0 to 11 (12 addresses per request)
			struct diag_l2_14230 * dlproto;
			int txi;	//index into txbuf for constructing request

			dlproto=(struct diag_l2_14230 *)global_l2_conn->diag_l2_proto_data;
			if (dlproto->modeflags & ISO14230_SHORTHDR) {
				printf("Using short headers.\n");
				dlproto->modeflags &= ~ISO14230_LONGHDR;	//deactivate long headers
			} else {
				hackmode=0;	//won't work without short headers
			}


			printf("Starting dump from 0x%06X to 0x%06X.\n", nextaddr, maxaddr);

			txdata[0]=0xAC;
			txdata[1]=0x81;
			nisreq.len = 2;	//AC 81 : 2 bytes so far
			txi=2;
			linecur = 0;

			for (addr=nextaddr; addr <= maxaddr; addr++) {
				txdata[txi++]= 0x83;		//field type
				txdata[txi++]= (uint8_t) ((addr & 0xFF<<24) >>24);
				txdata[txi++]= (uint8_t) ((addr & 0xFF<<16) >>16);
				txdata[txi++]= (uint8_t) ((addr & 0xFF<<8) >>8);
				txdata[txi++]= (uint8_t) (addr & 0xFF);
				nisreq.len += 5;
				linecur += 1;

				//request 12 addresses at a time, or whatever's left at the end
				if ((linecur != 0x0c) && (addr != maxaddr))
					continue;

				printf("\n%06X: ", nextaddr);

				if (hackmode==1) {
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
						break;
					}
					//Here, we're guaranteed to have found 0xEC in the first 4 bytes we got. But we may
					//need to "purge" some extra bytes on the next read
					// hdr0 (hdr1) (hdr2) 0xEC 0x81 ck
					//
					extra = (3 + i - errval);	//bytes to purge. I think the formula is ok
					extra = (extra < 0) ? 0: extra;	//make sure >=0
				} else {
					//not hackmode:

					rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
					if (rxmsg==NULL) {
						printf("\nError: no resp to rqst AC @ %06X, err=%d\n", addr, errval);
						retryscore -= 20;
						break;	//leave for loop
					}
					if ((rxmsg->data[0] != 0xEC) || (rxmsg->len != 2) ||
							(rxmsg->fmt & DIAG_FMT_BADCS)) {
						printf("\nFatal : bad AC resp at addr=0x%X: %02X, len=%u\n", addr,
							rxmsg->data[0], rxmsg->len);
						diag_freemsg(rxmsg);
						retryscore -= 25;
						break;
					}
					diag_freemsg(rxmsg);
				}	//if hackmode
				//Here, we sent a AC 81 83 ... 83... request that was accepted.
				//We need to send 21 81 04 01 to get the data now
				txdata[0]=0x21;
				txdata[1]=0x81;
				txdata[2]=0x04;
				txdata[3]=0x01;
				nisreq.len=4;

				if (hackmode==1) {
					int i, rqok=0;	//default to fail
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
							break;
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
							break;
						}
						//and verify checksum. [i] points to 0x61;
						if (hackbuf[i+2+linecur] != diag_cks1(&hackbuf[i-1], 3+linecur)) {
							//this checksum will not work with long headers...
							printf("\nhack mode : bad 61 CS ! got %02X\n", hackbuf[i+2+linecur]);
							diag_data_dump(stdout, &hackbuf[i], linecur+3);
							retryscore -=20;
							break;
						}

					}	//if l2_send OK

						//We can now dump this to the file...
					if (fwrite(&(hackbuf[i+2]), 1, linecur, romdump) != linecur) {
						printf("Error writing file!\n");
						retryscore -= 50;
						break;
					}
					diag_data_dump(stdout, &hackbuf[i+2], linecur);
				} else {
					//not hack mode:
					rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
					if (rxmsg==NULL) {
						printf("\nFatal : did not get response at address %06X, err=%d\n", addr, errval);
						retryscore -= 20;
						break;	//leave for loop
					}
					if ((rxmsg->data[0] != 0x61) || (rxmsg->len != (2+linecur)) ||
							(rxmsg->fmt & DIAG_FMT_BADCS)) {
						printf("\nFatal : error at addr=0x%X: %02X, len=%u\n", addr,
							rxmsg->data[0], rxmsg->len);
						diag_freemsg(rxmsg);
						retryscore -= 25;
						break;
					}
					//Now we got the reply to SID 21 : 61 81 x x x ...
					if (fwrite(&(rxmsg->data[2]), 1, linecur, romdump) != linecur) {
						printf("\nError writing file!\n");
						diag_freemsg(rxmsg);
						retryscore -= 50;
						break;
					}
					diag_data_dump(stdout, &rxmsg->data[2], linecur);
				}	//end second if(hackmode)

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
				printf("\nFinished!\n");
				break;	//leave while()
			}
		}	//while retryscore>0
		fclose(romdump);

		if (retryscore <= 0) {
			//there was an error inside and no retries left
			printf("No more retries; addr=%06X.\n", addr);
		}
		break;	//case 4 : dump with AC
	case 7:
		//np 7 <scode>: same as np 6 but with the NPT_DDL algo.
		keyalg=1;
		if (argc != 3) {
			printf("SID27 test. usage: np 7 <scode>\n");
			return CMD_USAGE;
		}
		if ((sscanf(argv[2], "%x", &scode) != 1)) {
			printf("Did not understand %s\n", argv[2]);
			return CMD_USAGE;
		}
		//fall through ! cheat !
	case 6:
		//np 6 & 7 : attempt a SecurityAccess (SID 27), using selected algo.
		txdata[0]=0x27;
		txdata[1]=0x01;	//RequestSeed
		nisreq.len=2;
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
		break;	//case 6,7 (sid27)
	default:
		return CMD_USAGE;
		break;
	}	//switch testnum


	return CMD_OK;
}
