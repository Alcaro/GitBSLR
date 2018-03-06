#include "socket.h"
#include "../test.h"

//TODO: fetch howsmyssl, ensure the only failure is the session cache

#ifdef ARLIB_TEST
#include "../os.h"

static void clienttest(cstring target, int port, bool ssl, bool xfail = false)
{
	test_skip("too slow");
	
	autoptr<runloop> loop = runloop::create();
	bool timeout = false;
	loop->set_timer_rel(8000, bind_lambda([&]()->bool { assert_unreachable(); loop->exit(); return false; }));
	
	autoptr<socket> sock = (ssl ? socket::create_ssl : socket::create)(target, port, loop);
	assert(sock);
	
	cstring http_get =
		"GET / HTTP/1.1\r\n"
		"Host: example.com\r\n"
		"Connection: close\r\n"
		"\r\n";
	
	assert_eq(sock->send(http_get.bytes()), http_get.length());
	
	uint8_t buf[8];
	size_t n_buf = 0;
	
	sock->callback(bind_lambda([&]()
		{
			if (n_buf < 8)
			{
				int bytes = sock->recv(arrayvieww<byte>(buf).slice(n_buf, n_buf==0 ? 2 : 1));
				if (bytes == 0) return;
				if (xfail)
				{
					assert_lt(bytes, 0);
					loop->exit();
				}
				else
				{
					assert_gte(bytes, 0);
					n_buf += bytes;
				}
			}
			if (n_buf >= 8)
			{
				uint8_t discard[4096];
				sock->recv(discard);
				loop->exit();
			}
		}));
	
	loop->enter();
	assert(!timeout);
	if (!xfail) assert_eq(string(arrayview<byte>(buf)), "HTTP/1.1");
}

//this IP is www.nic.ad.jp, both lookup and ping time for that one are 300ms
test("TCP client with IP",  "runloop", "tcp") { clienttest("192.41.192.145", 80, false); }
test("TCP client with DNS", "dns",     "tcp") { clienttest("www.nic.ad.jp", 80, false); }

test("SSL client",          "tcp",     "ssl") { clienttest("google.com", 443, true); }
test("SSL SNI",             "tcp",     "ssl") { clienttest("git.io", 443, true); }

test("TCP client, disconnecting server", "runloop,dns", "tcp")
{
	test_skip("too slow");
	test_inconclusive("30 seconds is too slow");
	return;
	
	autoptr<runloop> loop = runloop::create();
	
	loop->set_timer_rel(60000, bind_lambda([&]()->bool { loop->exit(); assert_ret(!"timeout", false); return false; }));
	
	uint64_t start_ms = time_ms_ne();
	
	//need to provoke Shitty Server into actually dropping the connections
	autoptr<socket> sock2;
	loop->set_timer_rel(5000, bind_lambda([&]()->bool { sock2 = socket::create("floating.muncher.se", 9, loop); return true; }));
	
	autoptr<socket> sock = socket::create("floating.muncher.se", 9, loop);
	assert(sock);
	
	sock->callback(bind_lambda([&]()
		{
			uint8_t buf[64];
			int ret = sock->recv(buf);
			assert_lte(ret, 0);
			if (ret < 0) loop->exit();
		}), NULL);
	
	loop->enter();
	
	//test passes if this thing notices that Shitty Server dropped it within 60 seconds
	//Shitty will drop me after 10-20 seconds, depending on when exactly the other sockets connect
	//Arlib will send a keepalive after 30 seconds, which will immediately return RST and return ECONNRESET
	
	//make sure it didn't fail because Shitty Server is down
	assert_gt(time_ms_ne()-start_ms, 29000);
}

//TODO: test permissiveness for those
test("SSL client, bad root", "tcp", "ssl") { clienttest("superfish.badssl.com", 443, true, true); }
test("SSL client, bad name", "tcp", "ssl") { clienttest("wrong.host.badssl.com", 443, true, true); }
test("SSL client, expired",  "tcp", "ssl") { clienttest("expired.badssl.com", 443, true, true); }

#ifdef ARLIB_SSL_BEARSSL
/*
static void ser_test(autoptr<socketssl>& s)
{
//int fd;
//s->serialize(&fd);
	
	socketssl* sp = s.release();
	assert(sp);
	assert(!s);
	int fd;
	array<byte> data = sp->serialize(&fd);
	assert(data);
	s = socketssl::deserialize(fd, data);
	assert(s);
}

test("SSL serialization")
{
	test_skip("too slow");
	autoptr<socketssl> s = socketssl::create("google.com", 443);
	testcall(ser_test(s));
	s->send("GET / HTTP/1.1\n");
	testcall(ser_test(s));
	s->send("Host: google.com\nConnection: close\n\n");
	testcall(ser_test(s));
	array<byte> bytes = recvall(s, 4);
	assert_eq(string(bytes), "HTTP");
	testcall(ser_test(s));
	bytes = recvall(s, 4);
	assert_eq(string(bytes), "/1.1");
}
*/
#endif
#endif
