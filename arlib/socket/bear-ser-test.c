/*
run (Unix only, only tested on Linux) with:

gcc -std=c99 bear-ser.c bear-ser-test.c ../deps/bearssl-0.5/build/libbearssl.a -O0 -g -o bear-ser-a &&
gcc -std=c99 bear-ser.c bear-ser-test.c ../deps/bearssl-0.5/build/libbearssl.a -O2 -g -o bear-ser-b &&
gcc -std=c99 bear-ser.c bear-ser-test.c ../deps/bearssl-0.5/build/libbearssl.a -O3 -g -o bear-ser-c &&
./bear-ser-a

yes, compile thrice, this makes functions move around and tests that freeze/unfreeze handles this properly
be careful about BearSSL versions; while I haven't needed to change this between 0.3, 0.4 and 0.5,
it's pointless to take risks

expected output:
some technical crap, along with 'HTTP/1.1 500 Domain Not Found', associated headers and body; then
another 'HTTP/1.1 500 Domain Not Found', but this one's headers/body won't terminate
*/

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include "../deps/bearssl-0.5/inc/bearssl.h"

typedef struct br_frozen_ssl_client_context_ {
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
} br_frozen_ssl_client_context;

void br_ssl_client_freeze(br_frozen_ssl_client_context* fr, const br_ssl_client_context* cc, const br_x509_minimal_context* xc);
void br_ssl_client_unfreeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc);

struct state {
	br_ssl_client_context sc;
	br_x509_minimal_context xc;
	uint8_t iobuf[BR_SSL_BUFSIZE_BIDI];
} s, ref;
struct state_fr {
	br_frozen_ssl_client_context sc;
	uint8_t iobuf[BR_SSL_BUFSIZE_BIDI];
};


//this is /etc/ssl/certs/GlobalSign_Root_CA.pem, as used by xkcd.com
static const unsigned char TA0_DN[] = {
        0x30, 0x57, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
        0x02, 0x42, 0x45, 0x31, 0x19, 0x30, 0x17, 0x06, 0x03, 0x55, 0x04, 0x0A,
        0x13, 0x10, 0x47, 0x6C, 0x6F, 0x62, 0x61, 0x6C, 0x53, 0x69, 0x67, 0x6E,
        0x20, 0x6E, 0x76, 0x2D, 0x73, 0x61, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03,
        0x55, 0x04, 0x0B, 0x13, 0x07, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x43, 0x41,
        0x31, 0x1B, 0x30, 0x19, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x12, 0x47,
        0x6C, 0x6F, 0x62, 0x61, 0x6C, 0x53, 0x69, 0x67, 0x6E, 0x20, 0x52, 0x6F,
        0x6F, 0x74, 0x20, 0x43, 0x41
};

static const unsigned char TA0_RSA_N[] = {
        0xDA, 0x0E, 0xE6, 0x99, 0x8D, 0xCE, 0xA3, 0xE3, 0x4F, 0x8A, 0x7E, 0xFB,
        0xF1, 0x8B, 0x83, 0x25, 0x6B, 0xEA, 0x48, 0x1F, 0xF1, 0x2A, 0xB0, 0xB9,
        0x95, 0x11, 0x04, 0xBD, 0xF0, 0x63, 0xD1, 0xE2, 0x67, 0x66, 0xCF, 0x1C,
        0xDD, 0xCF, 0x1B, 0x48, 0x2B, 0xEE, 0x8D, 0x89, 0x8E, 0x9A, 0xAF, 0x29,
        0x80, 0x65, 0xAB, 0xE9, 0xC7, 0x2D, 0x12, 0xCB, 0xAB, 0x1C, 0x4C, 0x70,
        0x07, 0xA1, 0x3D, 0x0A, 0x30, 0xCD, 0x15, 0x8D, 0x4F, 0xF8, 0xDD, 0xD4,
        0x8C, 0x50, 0x15, 0x1C, 0xEF, 0x50, 0xEE, 0xC4, 0x2E, 0xF7, 0xFC, 0xE9,
        0x52, 0xF2, 0x91, 0x7D, 0xE0, 0x6D, 0xD5, 0x35, 0x30, 0x8E, 0x5E, 0x43,
        0x73, 0xF2, 0x41, 0xE9, 0xD5, 0x6A, 0xE3, 0xB2, 0x89, 0x3A, 0x56, 0x39,
        0x38, 0x6F, 0x06, 0x3C, 0x88, 0x69, 0x5B, 0x2A, 0x4D, 0xC5, 0xA7, 0x54,
        0xB8, 0x6C, 0x89, 0xCC, 0x9B, 0xF9, 0x3C, 0xCA, 0xE5, 0xFD, 0x89, 0xF5,
        0x12, 0x3C, 0x92, 0x78, 0x96, 0xD6, 0xDC, 0x74, 0x6E, 0x93, 0x44, 0x61,
        0xD1, 0x8D, 0xC7, 0x46, 0xB2, 0x75, 0x0E, 0x86, 0xE8, 0x19, 0x8A, 0xD5,
        0x6D, 0x6C, 0xD5, 0x78, 0x16, 0x95, 0xA2, 0xE9, 0xC8, 0x0A, 0x38, 0xEB,
        0xF2, 0x24, 0x13, 0x4F, 0x73, 0x54, 0x93, 0x13, 0x85, 0x3A, 0x1B, 0xBC,
        0x1E, 0x34, 0xB5, 0x8B, 0x05, 0x8C, 0xB9, 0x77, 0x8B, 0xB1, 0xDB, 0x1F,
        0x20, 0x91, 0xAB, 0x09, 0x53, 0x6E, 0x90, 0xCE, 0x7B, 0x37, 0x74, 0xB9,
        0x70, 0x47, 0x91, 0x22, 0x51, 0x63, 0x16, 0x79, 0xAE, 0xB1, 0xAE, 0x41,
        0x26, 0x08, 0xC8, 0x19, 0x2B, 0xD1, 0x46, 0xAA, 0x48, 0xD6, 0x64, 0x2A,
        0xD7, 0x83, 0x34, 0xFF, 0x2C, 0x2A, 0xC1, 0x6C, 0x19, 0x43, 0x4A, 0x07,
        0x85, 0xE7, 0xD3, 0x7C, 0xF6, 0x21, 0x68, 0xEF, 0xEA, 0xF2, 0x52, 0x9F,
        0x7F, 0x93, 0x90, 0xCF
};

static const unsigned char TA0_RSA_E[] = {
        0x01, 0x00, 0x01
};

static const br_x509_trust_anchor TAs[1] = {
        {
                { (unsigned char *)TA0_DN, sizeof TA0_DN },
                BR_X509_TA_CA,
                {
                        BR_KEYTYPE_RSA,
                        { .rsa = {
                                (unsigned char *)TA0_RSA_N, sizeof TA0_RSA_N,
                                (unsigned char *)TA0_RSA_E, sizeof TA0_RSA_E,
                        } }
                }
        }
};

#define TAs_NUM   1



void appdata_init(const char * host, const char * port);
void appdata_send(const uint8_t* data, size_t len);
int appdata_recv(uint8_t* data, size_t len);

void brdata_process()
{
	size_t buflen;
	uint8_t* buf = br_ssl_engine_sendrec_buf(&s.sc.eng, &buflen);
	if (buflen > 0)
	{
		appdata_send(buf, buflen);
		br_ssl_engine_sendrec_ack(&s.sc.eng, buflen);
	}
	
	buf = br_ssl_engine_recvrec_buf(&s.sc.eng, &buflen);
	if (buflen)
	{
		int bytes = appdata_recv(buf, buflen);
		if (bytes > 0) br_ssl_engine_recvrec_ack(&s.sc.eng, bytes);
	}
}

void brdata_send_raw(const uint8_t* data, size_t len)
{
	if (!len) return;
	
again: ;
	size_t buflen;
	uint8_t* buf = br_ssl_engine_sendapp_buf(&s.sc.eng, &buflen);
	if (buflen > len) buflen = len;
	if (buflen == 0)
	{
		brdata_process();
		goto again;
	}
	
	memcpy(buf, data, buflen);
	br_ssl_engine_sendapp_ack(&s.sc.eng, buflen);
	br_ssl_engine_flush(&s.sc.eng, false);
	brdata_process();
}

int brdata_recv_raw(uint8_t* data, size_t len)
{
	if (!len) return 0;
	
again: ;
	size_t buflen;
	uint8_t* buf = br_ssl_engine_recvapp_buf(&s.sc.eng, &buflen);
	if (buflen > len) buflen = len;
	if (buflen == 0)
	{
		brdata_process();
		goto again;
	}
	
	memcpy(data, buf, buflen);
	br_ssl_engine_recvapp_ack(&s.sc.eng, buflen);
	brdata_process();
	return buflen;
}

void brdata_send(const char * data)
{
	brdata_send_raw((uint8_t*)data, strlen(data));
}

void brdata_recv_print(size_t len)
{
	while (len)
	{
		uint8_t buf[2048];
		int nlen = brdata_recv_raw(buf, len);
		if (nlen<0) break;
		fwrite(buf, 1,nlen, stdout);
		len -= nlen;
	}
}



struct {
	const char * send;
	int recv;
	const char * next;
} ops[] = {
	//intentionally no Host: header
	//this yields a short (450 bytes) error message, which is easy to inspect on the terminal
	{ NULL,                 0, "./bear-ser-b" },
	{ "GET / HTTP/1.1\r\n", 0, "./bear-ser-c" },
	{ "\r\nGET ",           0, "./bear-ser-a" }, // do messy stuff with the requests, just to make things harder
	{ "",                 300, "./bear-ser-b" }, // read a bit less than the error size, to ensure we don't overshoot
	{ "/ HTTP/1.1\r\n",     0, "./bear-ser-c" },
	{ "\r\n",               0, "./bear-ser-a" },
	{ "",                 500, NULL           }, // read a bit more than last time because why not
};

void loopback()
{
	//make sure serializing and unserializing to same address doesn't change anything
	struct state s_;
	memcpy(&s_, &s, sizeof(s_));
	
	struct state_fr fr;
	br_ssl_client_freeze(&fr.sc, &s.sc, &s.xc);
	memcpy(fr.iobuf, s.iobuf, sizeof(fr.iobuf));
	
	memcpy(&s, &ref, sizeof(s));
	memcpy(s.iobuf, fr.iobuf, sizeof(s.iobuf));
	br_ssl_client_unfreeze(&fr.sc, &s.sc, &s.xc);
	
	uint8_t* sp=(uint8_t*)&s;
	uint8_t* sp_=(uint8_t*)&s_;
	bool bad = false;
	for (int i=0;i<sizeof(s);i++)
	{
		if (sp[i]!=sp_[i])
		{
			printf("[%i:%.2X,%.2X]",i,sp[i],sp_[i]);
			bad = true;
		}
	}
	if (bad) exit(1);
}

//used to replay a failed session, in case of crashes (not needed anymore, but kept just in case)
//the RNG states are serialized as well, so replaying the session will work
FILE* rcv_r;
FILE* rcv_w;
int _; // shut up gcc, I know what these files will return
int main(int argc, char** argv)
{
	int step;
	if (argv[1]) step=strtol(argv[1], NULL, 0);
	else step=0;
	
	printf("{{{TEXT=%p DATA=%p RDATA=%p", &br_tls12_sha256_prf, &s, &br_hmac_drbg_vtable);
	printf(" step=%i/%lu %s %s}}}\n", step+1, sizeof(ops)/sizeof(*ops), argv[0], argv[1]);
	
	br_ssl_client_init_full(&s.sc, &s.xc, TAs, TAs_NUM);
	br_ssl_engine_set_buffer(&s.sc.eng, s.iobuf, sizeof(s.iobuf), true);
	br_ssl_client_reset(&s.sc, "", false);
	memcpy(&ref, &s, sizeof(ref));
	
	if (step==0)
	{
		br_ssl_client_reset(&s.sc, "xkcd.com", false);
		appdata_init("xkcd.com", "443");
		
		remove("bear-last.bin");
	}
	else
	{
		FILE* f = fopen("bear-ser.bin", "rb");
		struct state_fr fr;
		_ = fread(&fr, sizeof(fr),1, f);
		fclose(f);
		
		memcpy(s.iobuf, fr.iobuf, sizeof(s.iobuf));
		br_ssl_client_unfreeze(&fr.sc, &s.sc, &s.xc);
	}
	
	rcv_r = fopen("bear-last.bin", "rb");
	if (!rcv_r) rcv_w = fopen("bear-last.bin", "wb");
	
	if (step!=0)
	{
		loopback();
		brdata_send(ops[step].send);
		loopback();
		brdata_recv_print(ops[step].recv);
		loopback();
	}
	
	if (rcv_r) fclose(rcv_r);
	if (rcv_w) { fclose(rcv_w); remove("bear-last.bin"); }
	
	if (ops[step].next)
	{
		struct state_fr fr;
		br_ssl_client_freeze(&fr.sc, &s.sc, &s.xc);
		memcpy(fr.iobuf, s.iobuf, sizeof(fr.iobuf));
		
		FILE* f = fopen("bear-ser.bin", "wb");
		_ = fwrite(&fr, sizeof(fr),1, f);
		fclose(f);
		
		char step_s[10];
		sprintf(step_s, "%i", step+1);
		execl(ops[step].next, ops[step].next, step_s, NULL);
		perror("exec");
	}
	else remove("bear-ser.bin");
}


#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>

void appdata_init(const char * host, const char * port)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	
	struct addrinfo * addr = NULL;
	getaddrinfo(host, port, &hints, &addr);
	if (!addr) exit(1);
	
	int fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	connect(fd, addr->ai_addr, addr->ai_addrlen);
	freeaddrinfo(addr);
	
	if (fd!=3) exit(1); // 0/1/2 are used, so this socket will be 3
}
void appdata_send(const uint8_t* data, size_t len)
{
//errno=0;
//int i=
	send(3, data, len, 0);
//printf("<s=%i,%i,%i>\n",i,(int)len,errno);
}
int appdata_recv(uint8_t* data, size_t len)
{
errno=0;
	int ret;
	if (!rcv_r) ret = recv(3, data, len, MSG_DONTWAIT);
	else
	{
		_ = fread(&ret, 4,1, rcv_r);
		_ = fread(data, 1,ret, rcv_r);
	}
//if(errno!=11&&ret>0)printf("<r=%i,%i,%i>\n",ret,(int)len,errno);
	if (ret<0) ret=0;
	if (rcv_w && ret>=0)
	{
		_ = fwrite(&ret, 4,1, rcv_w);
		_ = fwrite(data, 1,ret, rcv_w);
		fflush(rcv_w);
	}
	return ret;
}
