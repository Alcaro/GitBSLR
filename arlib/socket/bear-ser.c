#ifdef __cplusplus
extern "C" {
#endif

#include "../deps/bearssl-0.5/inc/bearssl.h"

//This file exports the following two functions and struct:

typedef struct br_frozen_ssl_client_context_ {
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
} br_frozen_ssl_client_context;


//A frozen context can safely be transferred between processes.
//Don't use the context after freezing it, obviously. Deallocate it.
// The context will be scrambled. This is a bug, but since it's unusable post-freeze anyways, it's considered acceptable.
void br_ssl_client_freeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc);

//cc/xc must be initialized in the same way as the original objects prior to calling this.
//This includes, but is not limited to, the following functions (if used originally):
//- br_ssl_client_init_full
//- br_ssl_engine_set_x509
//- br_ssl_engine_set_buffer
//- br_ssl_client_reset (other than the domain name, which may differ, or even be NULL)
//- The list of trust anchors in the x509, including order
//Can not be used to upgrade BearSSL versions (not even tiny patches), due to T0-relative pointers.
// Likely to break on BearSSL versions other than 0.5, it depends on lots of implementation details. May work, but don't test your luck.
//The frozen context will be scrambled and may not be used afterwards.
// This is a bug, but since cloning a SSL context is illegal anyways, it's considered acceptable.
void br_ssl_client_unfreeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc);



static const uint8_t * br_t0_extract(void(*fn)(void* t0ctx))
{
	uint32_t rp[32]; // the init functions write some random stuff to rp
	struct {
		uint32_t * dp;
		uint32_t * rp;
		const uint8_t * ip;
	} cpu = { NULL, rp, NULL };
	fn(&cpu);
	return cpu.ip;
}

//I need to use these functions, which aren't part of the public API. (Thanks for not making x509's init static.)
//It's about nine million kinds of unsafe, but I have no other choice, other than changing upstream code...
extern void br_ssl_hs_client_init_main(void* ctx);
static const uint8_t * br_ssl_client_get_default_t0()
{
	return br_t0_extract(br_ssl_hs_client_init_main);
}

extern void br_x509_minimal_init_main(void* ctx);
static const uint8_t * br_x509_minimal_get_default_t0()
{
	return br_t0_extract(br_x509_minimal_init_main);
}


static void br_ssl_engine_freeze(br_ssl_engine_context* cc, const uint8_t * t0_init)
{
	if (cc->icbc_in && cc->in.vtable == &cc->icbc_in->inner)
	{
		cc->in.vtable = (br_sslrec_in_class*)2;
		if (cc->in.cbc.bc.vtable == cc->iaes_cbcdec) cc->in.cbc.bc.vtable = (br_block_cbcdec_class*)1;
		if (cc->in.cbc.bc.vtable == cc->ides_cbcdec) cc->in.cbc.bc.vtable = (br_block_cbcdec_class*)2;
	}
	if (cc->igcm_in    && cc->in.vtable == &cc->igcm_in   ->inner) cc->in.vtable = (br_sslrec_in_class*)3;
	if (cc->ichapol_in && cc->in.vtable == &cc->ichapol_in->inner) cc->in.vtable = (br_sslrec_in_class*)4;
	
	if (cc->out.vtable == &br_sslrec_out_clear_vtable) cc->out.vtable = (br_sslrec_out_class*)1;
	if (cc->icbc_out && cc->out.vtable == &cc->icbc_out->inner)
	{
		cc->out.vtable = (br_sslrec_out_class*)2;
		if (cc->out.cbc.bc.vtable == cc->iaes_cbcenc) cc->out.cbc.bc.vtable = (br_block_cbcenc_class*)1;
		if (cc->out.cbc.bc.vtable == cc->ides_cbcenc) cc->out.cbc.bc.vtable = (br_block_cbcenc_class*)2;
	}
	if (cc->igcm_out    && cc->out.vtable == &cc->igcm_out   ->inner) cc->out.vtable = (br_sslrec_out_class*)3;
	if (cc->ichapol_out && cc->out.vtable == &cc->ichapol_out->inner) cc->out.vtable = (br_sslrec_out_class*)4;
	
	if (cc->rng.digest_class == cc->mhash.impl[0]) cc->rng.digest_class = (void*)1;
	if (cc->rng.digest_class == cc->mhash.impl[1]) cc->rng.digest_class = (void*)2;
	if (cc->rng.digest_class == cc->mhash.impl[2]) cc->rng.digest_class = (void*)3;
	if (cc->rng.digest_class == cc->mhash.impl[3]) cc->rng.digest_class = (void*)4;
	if (cc->rng.digest_class == cc->mhash.impl[4]) cc->rng.digest_class = (void*)5;
	if (cc->rng.digest_class == cc->mhash.impl[5]) cc->rng.digest_class = (void*)6;
	
	cc->cpu.dp -= ((uintptr_t)cc->dp_stack)/4; // /4 because these are u32 pointers
	cc->cpu.rp -= ((uintptr_t)cc->rp_stack)/4;
	// I can't find the buffer start, only a fixed point in it, so I can't be sure it won't subtract to null
	// but it probably won't hit size 100000
	if (cc->cpu.ip) cc->cpu.ip -= (uintptr_t)t0_init - 100000;
	
	if (cc->hbuf_in )       cc->hbuf_in        -= (uintptr_t)cc->ibuf - 1; // -1 to make sure hbuf_in==ibuf doesn't change hbuf_in==NULL
	if (cc->hbuf_out)       cc->hbuf_out       -= (uintptr_t)cc->obuf - 1;
	if (cc->saved_hbuf_out) cc->saved_hbuf_out -= (uintptr_t)cc->obuf - 1;
	
	//TODO: Do I need these?
	//const br_x509_certificate *chain;
	//size_t chain_len;
	//const unsigned char *cert_cur;
	//size_t cert_len;
}

static void br_ssl_engine_unfreeze(br_ssl_engine_context* cc, const br_ssl_engine_context* reference, const uint8_t * t0_init)
{
	cc->ibuf = reference->ibuf;
	cc->obuf = reference->obuf;
	
	cc->icbc_in     = reference->icbc_in;
	cc->igcm_in     = reference->igcm_in;
	cc->ichapol_in  = reference->ichapol_in;
	cc->icbc_out    = reference->icbc_out;
	cc->igcm_out    = reference->igcm_out;
	cc->ichapol_out = reference->ichapol_out;
	
	if (cc->in.vtable == (void*)2)
	{
		cc->in.vtable = &reference->icbc_in->inner;
		if (cc->in.cbc.bc.vtable == (void*)1) cc->in.cbc.bc.vtable = reference->iaes_cbcdec;
		if (cc->in.cbc.bc.vtable == (void*)2) cc->in.cbc.bc.vtable = reference->ides_cbcdec;
	}
	if (cc->in.vtable == (void*)3)
	{
		cc->in.vtable = &reference->igcm_in->inner;
		cc->in.gcm.bc.vtable = reference->iaes_ctr;
		cc->in.gcm.gh = reference->ighash;
	}
	if (cc->in.vtable == (void*)4)
	{
		cc->in.vtable = &reference->ichapol_in->inner;
		cc->in.chapol.ichacha = reference->ichacha;
		cc->in.chapol.ipoly = reference->ipoly;
	}
	if (cc->out.vtable == (void*)1)
	{
		cc->out.vtable = &br_sslrec_out_clear_vtable;
	}
	if (cc->out.vtable == (void*)2)
	{
		cc->out.vtable = &reference->icbc_out->inner;
		if (cc->out.cbc.bc.vtable == (void*)1) cc->out.cbc.bc.vtable = reference->iaes_cbcenc;
		if (cc->out.cbc.bc.vtable == (void*)2) cc->out.cbc.bc.vtable = reference->ides_cbcenc;
	}
	if (cc->out.vtable == (void*)3)
	{
		cc->out.vtable = &reference->igcm_out->inner;
		cc->out.gcm.bc.vtable = reference->iaes_ctr;
		cc->out.gcm.gh = reference->ighash;
	}
	if (cc->out.vtable == (void*)4)
	{
		cc->out.vtable = &reference->ichapol_out->inner;
		cc->out.chapol.ichacha = reference->ichacha;
		cc->out.chapol.ipoly = reference->ipoly;
	}
	
	if (cc->rng.vtable) cc->rng.vtable = &br_hmac_drbg_vtable;
	if (cc->rng.digest_class) cc->rng.digest_class = reference->mhash.impl[(uintptr_t)cc->rng.digest_class - 1];
	
	cc->cpu.dp += ((uintptr_t)reference->dp_stack)/4;
	cc->cpu.rp += ((uintptr_t)reference->rp_stack)/4;
	if (cc->cpu.ip) cc->cpu.ip += (uintptr_t)t0_init - 100000;
	if (cc->hbuf_in )       cc->hbuf_in        += (uintptr_t)reference->ibuf - 1;
	if (cc->hbuf_out)       cc->hbuf_out       += (uintptr_t)reference->obuf - 1;
	if (cc->saved_hbuf_out) cc->saved_hbuf_out += (uintptr_t)reference->obuf - 1;
	cc->hsrun = reference->hsrun;
	
	memcpy(&cc->mhash.impl, &reference->mhash.impl, sizeof(cc->mhash.impl));
	
	cc->x509ctx = reference->x509ctx;
	cc->protocol_names = reference->protocol_names;
	
	cc->prf10 = reference->prf10;
	cc->prf_sha256 = reference->prf_sha256;
	cc->prf_sha384 = reference->prf_sha384;
	cc->iaes_cbcenc = reference->iaes_cbcenc;
	cc->iaes_cbcdec = reference->iaes_cbcdec;
	cc->iaes_ctr = reference->iaes_ctr;
	cc->ides_cbcenc = reference->ides_cbcenc;
	cc->ides_cbcdec = reference->ides_cbcdec;
	cc->ighash = reference->ighash;
	cc->ichacha = reference->ichacha;
	cc->ipoly = reference->ipoly;
	cc->icbc_in = reference->icbc_in;
	cc->icbc_out = reference->icbc_out;
	cc->igcm_in = reference->igcm_in;
	cc->igcm_out = reference->igcm_out;
	cc->ichapol_in = reference->ichapol_in;
	cc->ichapol_out = reference->ichapol_out;
	cc->iec = reference->iec;
	cc->irsavrfy = reference->irsavrfy;
	cc->iecdsa = reference->iecdsa;
}


static void br_x509_minimal_freeze(br_x509_minimal_context* xc, const br_ssl_engine_context* engine)
{
	if (xc->pkey.key_type==BR_KEYTYPE_RSA)
	{
		if (xc->pkey.key.rsa.n) xc->pkey.key.rsa.n -= (uintptr_t)xc; // these point to either NULL, ->pkey_data or ->ee_pkey_data
		if (xc->pkey.key.rsa.e) xc->pkey.key.rsa.e -= (uintptr_t)xc;
	}
	else
	{
		if (xc->pkey.key.ec.q) xc->pkey.key.ec.q -= (uintptr_t)xc;
	}
	
	xc->cpu.dp -= ((uintptr_t)xc->dp_stack)/4;
	xc->cpu.rp -= ((uintptr_t)xc->rp_stack)/4;
	if (xc->cpu.ip) xc->cpu.ip -= (uintptr_t)br_x509_minimal_get_default_t0() - 100000;
	
	if (xc->hbuf) xc->hbuf -= (uintptr_t)engine; // points to ssl_engine->pad if set
}

static void br_x509_minimal_unfreeze(br_x509_minimal_context* xc, const br_x509_minimal_context* reference, const br_ssl_engine_context* engine)
{
	xc->vtable = reference->vtable;
	
	if (xc->pkey.key_type==BR_KEYTYPE_RSA)
	{
		if (xc->pkey.key.rsa.n) xc->pkey.key.rsa.n += (uintptr_t)reference;
		if (xc->pkey.key.rsa.e) xc->pkey.key.rsa.e += (uintptr_t)reference;
	}
	else
	{
		if (xc->pkey.key.ec.q) xc->pkey.key.ec.q += (uintptr_t)reference;
	}
	
	xc->cpu.dp += ((uintptr_t)reference->dp_stack)/4;
	xc->cpu.rp += ((uintptr_t)reference->rp_stack)/4;
	if (xc->cpu.ip) xc->cpu.ip += (uintptr_t)br_x509_minimal_get_default_t0() - 100000;
	
	if (xc->server_name) xc->server_name = engine->server_name;
	
	if (xc->hbuf) xc->hbuf += (uintptr_t)engine;
	
	xc->trust_anchors = reference->trust_anchors;
	
	memcpy(&xc->mhash.impl, &reference->mhash.impl, sizeof(xc->mhash.impl));
	
	xc->dn_hash_impl = reference->dn_hash_impl;
	if (xc->dn_hash.vtable) xc->dn_hash.vtable = reference->dn_hash_impl;
	
	xc->name_elts = reference->name_elts;
	
	xc->irsa = reference->irsa;
	xc->iecdsa = reference->iecdsa;
	xc->iec = reference->iec;
}


void br_ssl_client_freeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc)
{
	br_ssl_engine_freeze(&cc->eng, br_ssl_client_get_default_t0());
	br_x509_minimal_freeze(xc, &cc->eng);
	memcpy(&fr->cc, cc, sizeof(fr->cc));
	memcpy(&fr->xc, xc, sizeof(fr->xc));
}

void br_ssl_client_unfreeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc)
{
	fr->cc.client_auth_vtable = cc->client_auth_vtable;
	//TODO: fr->cc.client_auth (maybe, I don't really need client auth)
	fr->cc.irsapub = cc->irsapub;
	br_ssl_engine_unfreeze(&fr->cc.eng, &cc->eng, br_ssl_client_get_default_t0());
	br_x509_minimal_unfreeze(&fr->xc, xc, &cc->eng);
	memcpy(cc, &fr->cc, sizeof(*cc));
	memcpy(xc, &fr->xc, sizeof(*xc));
}

#ifdef __cplusplus
}
#endif
