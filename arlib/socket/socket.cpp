#include "socket.h"
#include "../bytepipe.h"
#include "../dns.h"

#undef socket
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <mstcpip.h>
	#define MSG_NOSIGNAL 0
	#define MSG_DONTWAIT 0
	#define SOCK_CLOEXEC 0
	#define close closesocket
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
#else
	#include <netdb.h>
	#include <errno.h>
	#include <unistd.h>
	#include <fcntl.h>
	
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
#endif

//wrapper because 'socket' is a type in this code, so socket(2) needs another name
static int mksocket(int domain, int type, int protocol) { return socket(domain, type|SOCK_CLOEXEC, protocol); }
#define socket socket_t

namespace {

static void initialize()
{
#ifdef _WIN32 // lol
	static bool initialized = false;
	if (initialized) return;
	initialized = true;
	
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

static int fixret(int ret)
{
	if (ret > 0) return ret;
	if (ret == 0) return socket::e_closed;
#ifdef __unix__
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#endif
#ifdef _WIN32
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#endif
	return socket::e_broken;
}

static int setsockopt(int socket, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return ::setsockopt(socket, level, option_name, (char*)/*lol windows*/option_value, option_len);
}

static int setsockopt(int socket, int level, int option_name, int option_value)
{
	return setsockopt(socket, level, option_name, &option_value, sizeof(option_value));
}

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
static void MAYBE_UNUSED setblock(int fd, bool newblock)
{
#ifdef _WIN32
	u_long nonblock = !newblock;
	ioctlsocket(fd, FIONBIO, &nonblock);
#else
	int flags = fcntl(fd, F_GETFL, 0);
	flags &= ~O_NONBLOCK;
	if (!newblock) flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
#endif
}

static addrinfo * parse_hostname(cstring domain, int port, bool udp)
{
	char portstr[16];
	sprintf(portstr, "%i", port);
	
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	
	addrinfo * addr = NULL;
	getaddrinfo(domain.c_str(), portstr, &hints, &addr);
	
	return addr;
}

static int connect(cstring domain, int port)
{
	initialize();
	
	addrinfo * addr = parse_hostname(domain, port, false);
	if (!addr) return -1;
	
	//TODO: this probably fails on windows...
	int fd = mksocket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, addr->ai_protocol);
	if (fd < 0) return -1;
#ifndef _WIN32
	//because 30 second pauses are unequivocally detestable
	timeval timeout;
	timeout.tv_sec = 4;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#endif
	if (connect(fd, addr->ai_addr, addr->ai_addrlen) != 0 && errno != EINPROGRESS)
	{
		freeaddrinfo(addr);
		close(fd);
		return -1;
	}
	freeaddrinfo(addr);
	
#ifndef _WIN32
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, 1); // enable
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, 3); // ping count before the kernel gives up
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, 30); // seconds idle until it starts pinging
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, 10); // seconds per ping once the pings start
#else
	struct tcp_keepalive keepalive = {
		1,       // SO_KEEPALIVE
		30*1000, // TCP_KEEPIDLE in milliseconds
		3*1000,  // TCP_KEEPINTVL
		//On Windows Vista and later, the number of keep-alive probes (data retransmissions) is set to 10 and cannot be changed.
		//https://msdn.microsoft.com/en-us/library/windows/desktop/dd877220(v=vs.85).aspx
		//so no TCP_KEEPCNT; I'll reduce INTVL instead. And a polite server will RST anyways.
	};
	u_long ignore;
	WSAIoctl(fd, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &ignore, NULL, NULL);
#endif
	
	return fd;
}

} // close namespace

namespace {

class socket_raw : public socket {
public:
	socket_raw(int fd, runloop* loop) : fd(fd), loop(loop) {}
	
	int fd;
	runloop* loop = NULL;
	function<void()> cb_read;
	function<void()> cb_write;
	
	static socket* create(int fd, runloop* loop)
	{
		if (fd<0) return NULL;
		return new socket_raw(fd, loop);
	}
	
	int recv(arrayvieww<byte> data)
	{
		return fixret(::recv(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL));
	}
	
	int send(arrayview<byte> data)
	{
		return fixret(::send(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL));
	}
	
	/*private*/ void on_readable(uintptr_t) { cb_read(); }
	/*private*/ void on_writable(uintptr_t) { cb_write(); }
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		loop->set_fd(fd,
		             cb_read  ? bind_this(&socket_raw::on_readable) : NULL,
		             cb_write ? bind_this(&socket_raw::on_writable) : NULL);
	}
	
	~socket_raw()
	{
		loop->set_fd(fd, NULL, NULL);
		close(fd);
	}
};

class socket_raw_udp : public socket {
public:
	socket_raw_udp(int fd, sockaddr * addr, socklen_t addrlen, runloop* loop)
		: fd(fd), loop(loop), peeraddr((uint8_t*)addr, addrlen)
	{
		peeraddr_cmp.resize(addrlen);
	}
	
	int fd;
	
	runloop* loop;
	function<void()> cb_read;
	function<void()> cb_write;
	
	array<byte> peeraddr;
	array<byte> peeraddr_cmp;
	
	int recv(arrayvieww<byte> data)
	{
		socklen_t len = peeraddr_cmp.size();
		int ret = fixret(::recvfrom(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL, (sockaddr*)peeraddr_cmp.ptr(), &len));
		//discard data from unexpected sources. source IPs can be forged under UDP, but probably helps a little
		//TODO: may be better to implement recvfrom as an actual function on those sockets
		if (len != peeraddr.size() || peeraddr != peeraddr_cmp) return 0;
		return ret;
	}
	
	int send(arrayview<byte> data)
	{
		return fixret(::sendto(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL, (sockaddr*)peeraddr.ptr(), peeraddr.size()));
	}
	
	/*private*/ void on_readable(uintptr_t) { cb_read(); }
	/*private*/ void on_writable(uintptr_t) { cb_write(); }
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		loop->set_fd(fd,
		             cb_read  ? bind_this(&socket_raw_udp::on_readable) : NULL,
		             cb_write ? bind_this(&socket_raw_udp::on_writable) : NULL);
	}
	
	~socket_raw_udp()
	{
		loop->set_fd(fd, NULL, NULL);
		close(fd);
	}
};

//A flexible socket sends a DNS request, then seamlessly opens a TCP or SSL connection to the returned IP.
class socket_flex : public socket {
public:
	socket* i_connect(cstring domain, cstring ip, int port)
	{
		socket* ret = socket_raw::create(connect(ip, port), this->loop);
		if (ret && this->ssl) ret = socket::wrap_ssl(ret, domain, this->loop);
		return ret;
	}
	
	socket_flex(cstring domain, int port, runloop* loop, bool ssl)
	{
		this->loop = loop;
		this->ssl = ssl;
		child = i_connect(domain, domain, port);
		if (!child)
		{
			this->port = port;
			dns = new DNS(loop);
			dns->resolve(domain, bind_this(&socket_flex::dns_cb));
		}
		set_loop();
	}
	
	autoptr<DNS> dns;
	uint16_t port;
	bool ssl;
	
	runloop* loop = NULL;
	autoptr<socket> child;
	
	function<void()> cb_read;
	function<void()> cb_write; // call once when connection is ready, or forever if connection is broken
	
	/*private*/ void dns_cb(string domain, string ip)
	{
		child = i_connect(domain, ip, port);
		dns = NULL;
		set_loop();
		cb_write(); // TODO: do this later
	}
	
	/*private*/ void set_loop()
	{
		if (child) child->callback(cb_read, cb_write);
	}
	
	int recv(arrayvieww<byte> data)
	{
		if (!child)
		{
			if (dns) return 0;
			else return e_broken;
		}
		return child->recv(data);
	}
	
	int send(arrayview<byte> data)
	{
		if (!child)
		{
			if (dns) return 0;
			else return e_broken;
		}
		return child->send(data);
	}
	
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		
		set_loop();
	}
};

//Makes writes always succeed. If they fail, they're buffered in memory.
class socketbuf : public socket {
public:
	socketbuf(socket* child, runloop* loop) : loop(loop), child(child) {}
	
	runloop* loop;
	autoptr<socket> child;
	
	bytepipe tosend;
	uintptr_t idle_id = 0;
	function<void()> cb_read;
	function<void()> cb_write;
	
	/*private*/ void cancel()
	{
		child = NULL;
		if (cb_read) cb_read();
		else cb_write();
	}
	
	/*private*/ void set_loop()
	{
		if (!child) return;
		child->callback(cb_read, tosend.remaining() ? bind_this(&socketbuf::trysend_void) : NULL);
	}
	/*private*/ bool trysend(bool in_runloop)
	{
		arrayview<byte> bytes = tosend.pull_buf();
		if (!bytes.size()) return true;
		int nbytes = child->send(bytes);
		if (nbytes < 0)
		{
			if (in_runloop) cancel();
			return false;
		}
		tosend.pull_done(nbytes);
		
		set_loop();
		return true;
	}
	/*private*/ void trysend_void()
	{
		trysend(true);
	}
	
	int recv(arrayvieww<byte> data)
	{
		if (!child) return e_broken;
		return child->recv(data);
	}
	
	int send(arrayview<byte> data)
	{
		if (!child) return e_broken;
		tosend.push(data);
		if (!trysend(false)) return -1;
		return data.size();
	}
	
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		set_loop();
	}
};

} // close namespace

socket* socket::create(cstring domain, int port, runloop* loop)
{
	return new socketbuf(new socket_flex(domain, port, loop, false), loop);
}

socket* socket::create_ssl(cstring domain, int port, runloop* loop)
{
	return new socketbuf(new socket_flex(domain, port, loop, true), loop);
}

socket* socket::create_udp(cstring domain, int port, runloop* loop)
{
	initialize();
	
	addrinfo * addr = parse_hostname(domain, port, true);
	if (!addr) return NULL;
	
	int fd = mksocket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, addr->ai_protocol);
	socket* ret = new socket_raw_udp(fd, addr->ai_addr, addr->ai_addrlen, loop);
	freeaddrinfo(addr);
	//TODO: add the wrapper
	
	return ret;
}

#if 0
static MAYBE_UNUSED int socketlisten_create_ip4(int port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	
	int fd = mksocket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) goto fail;
	
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

static int socketlisten_create_ip6(int port)
{
	struct sockaddr_in6 sa; // IN6ADDR_ANY_INIT should work, but doesn't.
	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = in6addr_any;
	sa.sin6_port = htons(port);
	
	int fd = mksocket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, false) < 0) goto fail;
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

socketlisten* socketlisten::create(int port)
{
	initialize();
	
	int fd = -1;
	if (fd<0) fd = socketlisten_create_ip6(port);
#if defined(_WIN32) && _WIN32_WINNT < 0x0600
	//Windows XP can't dualstack the v6 addresses, so let's keep the fallback
	if (fd<0) fd = socketlisten_create_ip4(port);
#endif
	if (fd<0) return NULL;
	
	setblock(fd, false);
	return new socketlisten(fd);
}

socket* socketlisten::accept()
{
	return socket_wrap(::accept(this->fd, NULL,NULL));
}

socketlisten::~socketlisten() { if (loop) loop->set_fd(fd, NULL, NULL); close(this->fd); }
#endif

//what did I need this for?
//#ifdef __unix__
//namespace {
//class socket_pipe : public socket {
//public:
//	socket_pipe(int read, int write, runloop* loop) : loop(loop), fd_read(read), fd_write(write) {}
//	
//	runloop* loop;
//	
//	int fd_read;
//	int fd_write;
//	
//	function<void()> cb_read;
//	function<void()> cb_write;
//	
//	int recv(arrayvieww<byte> data)
//	{
//		return fixret(::read(this->fd_read, (char*)data.ptr(), data.size()));
//	}
//	
//	int send(arrayview<byte> data)
//	{
//		return fixret(::write(this->fd_write, (char*)data.ptr(), data.size()));
//	}
//	
//	/*private*/ void on_readable(uintptr_t) { cb_read(); }
//	/*private*/ void on_writable(uintptr_t) { cb_write(); }
//	void callback(function<void()> cb_read, function<void()> cb_write)
//	{
//		this->cb_read = cb_read;
//		this->cb_write = cb_write;
//		
//		if (fd_read >= 0) loop->set_fd(fd_read, cb_read ? bind_this(&socket_pipe::on_readable) : NULL, NULL);
//		if (fd_write >= 0) loop->set_fd(fd_write, NULL, cb_write ? bind_this(&socket_pipe::on_writable) : NULL);
//	}
//	
//	~socket_pipe()
//	{
//		if (fd_read >= 0)
//		{
//			loop->set_fd(fd_read, NULL, NULL);
//			close(fd_read);
//		}
//		if (fd_write >= 0)
//		{
//			loop->set_fd(fd_write, NULL, NULL);
//			close(fd_write);
//		}
//	}
//};
//} // close namespace
//
//socket* socket::create_from_pipe(int read, int write, runloop* loop)
//{
//	return new socket_pipe(read, write, loop);
//}
//
//#include"../test.h"
//test("","","") { test_expfail("figure out how to react to failure in socket_pipe::send"); }
//#endif
