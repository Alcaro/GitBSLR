#include "socket.h"

#ifdef ARLIB_SSL_TLSE
extern "C" {
#include "../deps/tlse.h"
}
#include "../file.h"
#include "../thread.h"

//TLSe flaws and limitations:
//- can't share root certs between contexts (with possible exception of tls_accept, didn't check)
//- lack of const on some functions
//- have to load root certs myself
//- had to implement Subject Alternative Name myself
//- turns out tls_consume_stream(buf_len=0) throws an error - and no debug output
//- tls_export_context return value seems be 'bytes expected' for inputlen=0 and inputlen>=expected,
//     but 'additional bytes expected' for inputlen=1
//- lacks extern "C" on header
//- lack of documentation

// separate context here to ensure they're not loaded multiple times, saves memory and time
static TLSContext* rootcerts;

RUN_ONCE_FN(initialize)
{
	rootcerts = tls_create_context(false, TLS_V12);
	
#ifdef __unix__
	array<byte> certs = file::read("/etc/ssl/certs/ca-certificates.crt");
	tls_load_root_certificates(rootcerts, certs.ptr(), certs.size());
#else
#error unsupported
#endif
}

class socketssl_impl : public socket {
public:
	socket* sock;
	TLSContext* ssl;
	bool permissive;
	
	//same as tls_default_verify, except tls_certificate_chain_is_valid_root is given another context
	static int verify(TLSContext* context, TLSCertificate* * certificate_chain, int len)
	{
		int err;
		for (int i = 0; i < len; i++) {
			err = tls_certificate_is_valid(certificate_chain[i]);
			if (err)
				return err;
		}
		
		err = tls_certificate_chain_is_valid(certificate_chain, len);
		if (err)
			return err;
		
		const char * sni = tls_sni(context);
		err = tls_certificate_valid_subject(certificate_chain[0], sni);
		if (err)
			return err;
		
		err = tls_certificate_chain_is_valid_root(rootcerts, certificate_chain, len);
		if (err)
			return err;
		
		//return certificate_expired;
		//return certificate_revoked;
		//return certificate_unknown;
		return no_error;
	}
	
	void process(bool block)
	{
		if (!sock) return;
		
		unsigned int outlen = 0;
		const uint8_t * out = tls_get_write_buffer(ssl, &outlen);
		if (out && outlen)
		{
			if (sock->send(arrayview<byte>(out, outlen)) < 0) { error(); return; }
			tls_buffer_clear(ssl);
		}
		
		uint8_t in[0x2000];
		int inlen = sock->recv(arrayvieww<byte>(in, sizeof(in)), block);
		if (inlen<0) { error(); return; }
		if (inlen>0) tls_consume_stream(ssl, in, inlen, (permissive ? NULL : verify));
	}
	
	void error()
	{
		delete sock;
		sock = NULL;
	}
	
	socketssl_impl(socket* parent) : sock(parent) {}
	static socketssl_impl* create(socket* parent, const char * domain, bool permissive)
	{
		if (!parent) return NULL;
		
		socketssl_impl* ret = new socketssl_impl(parent);
		ret->permissive = permissive;
		
		ret->ssl = tls_create_context(false, TLS_V12);
		
		tls_make_exportable(ret->ssl, true);
		tls_sni_set(ret->ssl, domain);
		
		tls_client_connect(ret->ssl);
		
		while (ret->sock && !tls_established(ret->ssl))
		{
			ret->process(true);
		}
		if (!ret->sock || tls_is_broken(ret->ssl))
		{
			delete ret;
			return NULL;
		}
		
		return ret;
	}
	
	int recv(arrayvieww<byte> data, bool block = false)
	{
	again:
		process(block);
		
		int ret = tls_read(ssl, data.ptr(), data.size());
		if (ret==0 && !sock) return e_broken;
		if (ret==0 && block) goto again;
		
		return ret;
	}
	
	int sendp(arrayview<byte> data, bool block = true)
	{
	again:
		if (!sock) return -1;
		
		int ret = tls_write(ssl, (uint8_t*)data.ptr(), data.size());
		process(false);
		if (ret==0 && block) goto again;
		return ret;
	}
	
	//bool active(bool want_recv, bool want_send)
	//{
	//	if (want_recv) return SSL_pending(ssl); // not available through the native interface...?
	//	else return false;
	//}
	
	~socketssl_impl()
	{
		if (ssl && sock)
		{
			tls_close_notify(ssl);
			process(false);
		}
		if (ssl) tls_destroy_context(ssl);
		if (sock) delete sock;
	}
	
	
	array<byte> serialize(int* fd)
	{
		array<byte> bytes;
		int len = tls_export_context(ssl, NULL, 0, false);
		if (len <= 0) return NULL;
		bytes.resize(len);
		int ret = tls_export_context(ssl, bytes.ptr(), bytes.size(), false);
		if (ret <= 0) return NULL;
		
		tls_destroy_context(this->ssl);
		this->ssl = NULL;
		
		*fd = decompose(&this->sock);
		
		delete this;
		return bytes;
	}
	
	//deserializing constructor
	socketssl_impl(int fd, arrayview<byte> data) : socketssl(fd)
	{
		this->sock = socket::create_from_fd(fd);
		this->ssl = tls_import_context((byte*)data.ptr(), data.size());
	}
};

socketssl* socketssl::create(socket* parent, cstring domain, bool permissive)
{
	initialize();
	return socketssl_impl::create(parent, domain, permissive);
}

array<byte> socketssl::serialize(int* fd)
{
	return ((socketssl_impl*)this)->serialize(fd);
}
socketssl* socketssl::deserialize(int fd, arrayview<byte> data)
{
	initialize();
	return new socketssl_impl(fd, data);
}
#endif
