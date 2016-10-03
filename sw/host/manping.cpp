////////////////////////////////////////////////////////////////////////////////
//
// Filename: 	manping.cpp
//
// Project:	OpenArty, an entirely open SoC based upon the Arty platform
//
// Purpose:	To command the network to ping a target.
//
//
// Creator:	Dan Gisselquist, Ph.D.
//		Gisselquist Technology, LLC
//
////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2015-2016, Gisselquist Technology, LLC
//
// This program is free software (firmware): you can redistribute it and/or
// modify it under the terms of  the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at
// your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTIBILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program.  (It's in the $(ROOT)/doc directory, run make with no
// target there if the PDF file isn't present.)  If not, see
// <http://www.gnu.org/licenses/> for a copy.
//
// License:	GPL, v3, as defined and found on www.gnu.org,
//		http://www.gnu.org/licenses/gpl.html
//
//
////////////////////////////////////////////////////////////////////////////////
//
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#include "port.h"
#include "regdefs.h"

#define	TXGO		0x04000
#define	NOHWCRC		0x08000
#define	NOHWMAC		0x10000
#define	NETRESET	0x20000

//
// Define DONT_INVERT for debugging only, as it will break the interface
// test
//
// #define	DONT_INVERT


FPGA	*m_fpga;
void	closeup(int v) {
	m_fpga->kill();
	exit(0);
}

void	usage(void) {
	printf("USAGE: manping EN:RX:xx:xx:xx:xx AR:TY:EN:TX:xx:xx de.st.ip.x ar.ty.ip.x\n");
}

bool	strtoenetaddr(char *s, unsigned char *addr) {
	char	*p, *c;

	p = s;
	addr[0] = (unsigned char)(strtoul(p, NULL, 16)&0x0ff);
	c = strchr(p,':');
	if((!c) || ((c-p)>=3))
		return false;

	p = c+1;
	addr[1] = (unsigned char)(strtoul(p, NULL, 16)&0x0ff);
	c = strchr(p,':');
	if((!c) || ((c-p)>=3))
		return false;

	p = c+1;
	addr[2] = (unsigned char)(strtoul(p, NULL, 16)&0x0ff);
	c = strchr(p,':');
	if((!c) || ((c-p)>=3))
		return false;

	p = c+1;
	addr[3] = (unsigned char)(strtoul(p, NULL, 16)&0x0ff);
	c = strchr(p,':');
	if((!c) || ((c-p)>=3))
		return false;

	p = c+1;
	addr[4] = (unsigned char)(strtoul(p, NULL, 16)&0x0ff);
	c = strchr(p,':');
	if((!c) || ((c-p)>=3))
		return false;

	p = c+1;
	addr[5] = (unsigned char)(strtoul(p, NULL, 16)&0x0ff);

	return true;
}

bool	strtoinetaddr(char *s, unsigned char *addr) {
	char	*p, *c;

	p = s;
	addr[0] = (unsigned char)(strtoul(p, NULL, 10)&0x0ff);
	c = strchr(p,'.');
	if((!c) || ((c-p)>3))
		return false;

	p = c+1;
	addr[1] = (unsigned char)(strtoul(p, NULL, 10)&0x0ff);
	c = strchr(p,'.');
	if((!c) || ((c-p)>3))
		return false;

	p = c+1;
	addr[2] = (unsigned char)(strtoul(p, NULL, 10)&0x0ff);
	c = strchr(p,'.');
	if((!c) || ((c-p)>3))
		return false;

	p = c+1;
	addr[3] = (unsigned char)(strtoul(p, NULL, 10)&0x0ff);

	return true;
}

unsigned	calccrc(const int bytelen, const unsigned *buf) {
	const unsigned int	taps = 0xedb88320u;
#ifdef	DONT_INVERT
	unsigned int	crc = 0;
#else
	unsigned int	crc = 0xffffffff; // initial value
#endif
	int	bidx;
	int	bp = 0;

	for(bidx = 0; bidx<bytelen; bidx++) {
		if (bidx == 14)
			bidx+=2;
		unsigned char	byte = buf[(bidx>>2)]>>(24-((bidx&3)<<3));

		// printf("CRC[%2d]: %02x ([%2d]0x%08x)\n", bidx, byte, (bidx>>2), buf[(bidx>>2)]);

		for(int bit=8; --bit>= 0; byte >>= 1) {
			if ((crc ^ byte) & 1) {
				crc >>= 1;
				crc ^= taps;
			} else
				crc >>= 1;
		} bp++;
	}
#ifndef	DONT_INVERT
	crc ^= 0xffffffff;
#endif
	// Now, we need to reverse these bytes
	// ABCD
	unsigned a,b,c,d;
	a = (crc>>24); // &0x0ff;
	b = (crc>>16)&0x0ff;
	c = (crc>> 8)&0x0ff;
	d = crc; // (crc    )&0x0ff;
	crc = (d<<24)|(c<<16)|(b<<8)|a;

	// printf("%d bytes processed\n", bp);
	return crc;
}

void	ipchecksum(unsigned *packet) {
	int npkt = (packet[0]>>24)&0x0f;
	unsigned checksum = 0;

	packet[2] &= 0xffff0000;
	printf("PKT[2] set to %08x\n", packet[2]);
	printf("checksum = %08x\n", checksum);
	for(int i=0; i<npkt; i++)
		checksum += packet[i] & 0x0ffff;
	printf("checksum = %08x\n", checksum);
	for(int i=0; i<npkt; i++)
		checksum += (packet[i]>>16)&0x0ffff;
	printf("checksum = %08x\n", checksum);
	checksum = (checksum & 0x0ffff) + (checksum >> 16);
	checksum = (checksum & 0x0ffff) + (checksum >> 16);
	packet[2] |= (checksum & 0x0ffff)^0x0ffff;

	printf("PKT[2] set to 0x%08x\n", packet[2]);
	checksum = 0;
	for(int i=0; i<npkt; i++)
		checksum += packet[i] & 0x0ffff;
	for(int i=0; i<npkt; i++)
		checksum += (packet[i]>>16)&0x0ffff;
	checksum = (checksum & 0x0ffff) + (checksum >> 16);
	checksum = (checksum & 0x0ffff) + (checksum >> 16);
	checksum ^= 0x0ffff;

	assert(checksum == 0);
}

void	clear_scope(FPGA *fpga) {
	unsigned	scopev;

	scopev = m_fpga->readio(R_NETSCOPE);
	int delay = (scopev>>20)&0x0f;
	delay = (1<<(delay))-32;
	m_fpga->writeio(R_NETSCOPE, (delay));
}

int main(int argc, char **argv) {
	int	skp=0, port = FPGAPORT;
	bool	config_hw_mac = false, config_hw_crc = true;
	FPGA::BUSW	txstat;
	int	argn;
	unsigned	checksum;
	unsigned	urand[16], nu = 0;

	{
		FILE *fp;
		for(int i=0; i<16; i++)
			urand[i] = rand();

		// Now, see if we can do better than the library random
		// number generator--but don't fail if we can't.
		fp = fopen("/dev/urandom", "r");
		if (fp != NULL) {
			int nr = fread(urand, sizeof(short), 16, fp);
			fclose(fp);
		}
	}
			

	FPGAOPEN(m_fpga);

	signal(SIGSTOP, closeup);
	signal(SIGHUP, closeup);

	txstat = m_fpga->readio(R_NET_TXCMD);

	// Take the ethernet out of reset
	if ((txstat & NETRESET) != 0)
		m_fpga->writeio(R_NET_TXCMD, (txstat &=(~NETRESET)));

	unsigned	packet[14];

	unsigned char	smac[6], dmac[6];
	unsigned char	sip[4],  dip[4];

#define	MYNET
#ifdef	MYNET
	// I know the ethernet MAC of the computer I wish to test with
	dmac[0] = 0xc8; dmac[1] = 0x3a; dmac[2] = 0x35;
	dmac[3] = 0xd2; dmac[4] = 0x07; dmac[5] = 0xb1;
	// And just something from /dev/urandom to create our source address
	smac[0] = 0xd2; smac[1] = 0xd8; smac[2] = 0x28;
	smac[3] = 0xe8; smac[4] = 0xb0; smac[5] = 0x96;

	// Similarly with the destination IP of the computer I wish to test with
	dip[0] = 192; dip[1] = 168; dip[2] = 10; dip[3] = 1;
	// and let's pick a source IP just ... somewhere on that network
	sip[0] = 192; sip[1] = 168; sip[2] = 10; sip[3] = 22;
#else
	dmac[0] = 0x00; dmac[1] = 0x0a; dmac[2] = 0xe6;
	dmac[3] = 0xf0; dmac[4] = 0x05; dmac[5] = 0xa3;
	smac[0] = 0x00; smac[1] = 0x12; smac[2] = 0x34;
	smac[3] = 0x56; smac[4] = 0x78; smac[5] = 0x90;

	packet[ 0] = 0x000ae6f0; packet[0] = 0xc83a35d2;
	packet[ 1] = 0x05a30012; packet[1] = 0x07b1d2d8;
	packet[ 2] = 0x34567890; packet[2] = 0x28e8b096;
	packet[ 3] = 0x08000800;
	packet[ 4] = 0x4500001c;
	packet[ 5] = 0xb3fe0000;
	packet[ 6] = 0x800172ba;
	packet[ 7] = 0x0a000003; packet[7] = 0xc0a80a16;
	packet[ 8] = 0x0a000002; packet[8] = 0xc0a80a01;
	packet[ 9] = 0x08000400;
	packet[10] = 0x001c894d;
	packet[11] = 0x00010203;
	packet[12] = 0x04050607;
	packet[13] = 0x08090a0b;
	packet[14] = 0x0c0d0e0f;
	packet[15] = 0x10111213;
	
	ipchecksum(&packet[4]);
	// packet[16] = calccrc(64, packet);

	for(int i=0; i<16; i++)
		printf("PKT[%02d] = 0x%08x\n", i, packet[i]);

	m_fpga->writei(R_NET_TXBUF, 14, packet);

		/*
		printf("Verilfying:\n");
		for(int i=0; i<ln; i++)
			printf("\t%2d: 0x%08x\n", i, (unsigned)m_fpga->readio(R_NET_TXBUF+i));
		*/

	m_fpga->writeio(R_NET_TXCMD, NOHWMAC|TXGO|(16<<2)); // Use the H/W CRC
	
	exit(EXIT_SUCCESS);
#endif

	clear_scope(m_fpga);

	argn = 1;

	{
		bool	bad_address = false;
		char	*badp = NULL;
		if ((argn<argc)&&(strchr(argv[argn], ':'))) {
			if (!strtoenetaddr(argv[argn++], dmac)) {
				badp = argv[argn-1];
				bad_address = true;
			} else if ((argn<argc)&&(strchr(argv[argn], ':'))) {
				if (!strtoenetaddr(argv[argn++], smac)) {
					badp = argv[argn-1];
					bad_address = true;
				}
			}
		} if ((argn<argc)&&(!bad_address)&&(strchr(argv[argn], '.'))) {
			if (!strtoinetaddr(argv[argn++], dip)) {
				badp = argv[argn-1];
				bad_address = true;
			} else if ((argn<argc)&&(strchr(argv[argn], '.'))) {
				if (!strtoinetaddr(argv[argn++], sip)) {
					badp = argv[argn-1];
					bad_address = true;
				}
			}
		}

		if (bad_address) {
			usage();
			fprintf(stderr, "ERR: could not comprehend address, %s\n", badp);
			exit(EXIT_FAILURE);
		}
	}

	printf("Building packet\n");
	printf("From %3d.%3d.%3d.%3d [%02x:%02x:%02x:%02x:%02x:%02x]\n",
		sip[0], sip[1], sip[2], sip[3],
		smac[0], smac[1], smac[2], smac[3], smac[4], smac[5]);
	printf("To   %3d.%3d.%3d.%3d [%02x:%02x:%02x:%02x:%02x:%02x]\n",
		dip[0], dip[1], dip[2], dip[3],
		dmac[0], dmac[1], dmac[2], dmac[3], dmac[4], dmac[5]);

	if (config_hw_mac) {
		int ln = 9;
		m_fpga->writeio(R_NET_MACHI, (smac[0]<<8)|(smac[1]));
		m_fpga->writeio(R_NET_MACLO, (smac[2]<<24)|(smac[3]<<16)|(smac[4]<<8)|(smac[5]));
			
		packet[ 0] = (dmac[0]<<24)|(dmac[1]<<16)|(dmac[2]<<8)|(dmac[3]);
		packet[ 1] = (dmac[4]<<24)|(dmac[5]<<16)|(smac[0]<<8)|(smac[1]);
		packet[ 2] = (smac[2]<<24)|(smac[3]<<16)|(smac[4]<<8)|(smac[5]);
		packet[ 3] = 0x08000800;
		packet[ 4] = 0x4500001c; // IPv4, 20byte header, type of service = 0
		packet[ 5] = (urand[nu++]&0xffff0000); // Packet ID
		packet[ 6] = 0x80010000; // no flags, fragment offset=0, ttl=0, proto=1
		packet[ 7] = (sip[0]<<24)|(sip[1]<<16)|(sip[2]<<8)|(sip[3]);
		packet[ 8] = (dip[0]<<24)|(dip[1]<<16)|(dip[2]<<8)|(dip[3]);
		packet[ 9] = 0x08000000;
		packet[10] = urand[nu++];
		packet[11] = 0;
		packet[12] = 0;
		packet[13] = 0;
		packet[14] = 0;

		ipchecksum(&packet[4]);

		checksum  =  packet[ 9] & 0x0ffff;
		checksum += (packet[ 9]>>16)&0x0ffff;
		checksum +=  packet[10] & 0x0ffff;
		checksum += (packet[10]>>16)&0x0ffff;
		checksum  = ((checksum >> 16)&0x0ffff) + (checksum & 0x0ffff);
		checksum  = ((checksum >> 16)&0x0ffff) + (checksum & 0x0ffff);
		packet[ 9] = ((packet[9] & 0xffff0000)|(checksum))^0x0ffff;

		packet[15] = calccrc(15*4, packet);

		// Now, let's rebuild our packet for the non-hw-mac option,
		// now that we know the CRC.
		packet[ 0] = (dmac[0]<<24)|(dmac[1]<<16)|(dmac[2]<<8)|(dmac[3]);
		packet[ 1] = (dmac[4]<<24)|(dmac[5]<<16)|0x8000;
		packet[ 2] = packet[ 4];
		packet[ 3] = packet[ 5];
		packet[ 4] = packet[ 6];
		packet[ 5] = packet[ 7];
		packet[ 6] = packet[ 8];
		packet[ 7] = packet[ 9];
		packet[ 8] = packet[10];
		packet[ 9] = packet[11];
		packet[10] = packet[12];
		packet[11] = packet[13];
		packet[12] = packet[14];
		packet[13] = packet[15];

		ln = (config_hw_crc)?9:14;
		printf("Packet:\n");
		for(int i=0; i<14; i++)
			printf("\t%2d: 0x%08x\n", i, packet[i]);

		m_fpga->writei(R_NET_TXBUF, ln, packet);

		/*
		printf("Verilfying:\n");
		printf("MAC: 0x%04x:0x%08x\n",
			m_fpga->readio(R_NET_MACHI),
			m_fpga->readio(R_NET_MACLO));
		for(int i=0; i<ln; i++)
			printf("\t%2d: 0x%08x\n", i, (unsigned)m_fpga->readio(R_NET_TXBUF+i));
		*/

		m_fpga->writeio(R_NET_TXCMD, TXGO|(ln<<2)|((config_hw_crc)?0:NOHWCRC));

	} else {
		int	ln;
		packet[ 0] = (dmac[0]<<24)|(dmac[1]<<16)|(dmac[2]<<8)|(dmac[3]);
		packet[ 1] = (dmac[4]<<24)|(dmac[5]<<16)|(smac[0]<<8)|(smac[1]);
		packet[ 2] = (smac[2]<<24)|(smac[3]<<16)|(smac[4]<<8)|(smac[5]);
		packet[ 3] = 0x08000800;
		packet[ 4] = 0x4500001c; // IPv4, 20byte header, type of service = 0
		packet[ 5] = (urand[nu++]&0xffff0000); // Packet ID
		packet[ 6] = 0x80010000; // no flags, fragment offset=0, ttl=0, proto=1
		packet[ 7] = (sip[0]<<24)|(sip[1]<<16)|(sip[2]<<8)|(sip[3]);
		packet[ 8] = (dip[0]<<24)|(dip[1]<<16)|(dip[2]<<8)|(dip[3]);
		packet[ 9] = 0x08000000;
		packet[10] = urand[nu++];
		packet[11] = 0;
		packet[12] = 0;
		packet[13] = 0;
		packet[14] = 0;

		ipchecksum(&packet[4]);

		checksum  =  packet[ 9] & 0x0ffff;
		checksum += (packet[ 9]>>16)&0x0ffff;
		checksum +=  packet[10] & 0x0ffff;
		checksum += (packet[10]>>16)&0x0ffff;
		checksum  = ((checksum >> 16)&0x0ffff) + (checksum & 0x0ffff);
		checksum  = ((checksum >> 16)&0x0ffff) + (checksum & 0x0ffff);
		packet[ 9] = ((packet[9] & 0xffff0000)|(checksum))^0x0ffff;


		packet[15] = calccrc(15*4, packet);

		ln = (config_hw_crc)?11:12;
		printf("Packet:\n");
		for(int i=0; i<15; i++)
			printf("\t%3d: 0x%08x\n", i, packet[i]);
		printf("\tCRC: 0x%08x\n", packet[15]);

		m_fpga->writei(R_NET_TXBUF, ln, packet);

		m_fpga->writeio(R_NET_TXCMD, TXGO|NOHWMAC|(ln<<2)|((config_hw_crc)?0:NOHWCRC));
	}

/*
	printf("\nLooking for a response ...\n");
	unsigned rxstat;
	int	errcount = 0;
	do {
		rxstat = m_fpga->readio(R_NET_RXCMD);
		if (rxstat & 0x04000) {
			int	rxlen;
			unsigned *buf;
			rxlen = rxstat & 0x03fff;
			buf = new unsigned[rxlen];
			m_fpga->readi(R_NET_RXBUF, rxlen, buf);
			for(int i=0; i<rxlen; i++)
				printf("\t0x%08x\n", buf[i]);
			delete[] buf;
			break;
		}
	} while((rxstat & 0x08000)&&(errcount++ < 50));

	if ((rxstat & 0x08000)&&(!(rxstat & 0x04000)))
		printf("Final Rx Status = %08x\n", rxstat);
*/
	
	delete	m_fpga;
}
