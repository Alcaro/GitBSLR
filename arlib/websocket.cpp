#ifdef ARLIB_SOCKET
#include "websocket.h"
#include "http.h"
#include "endian.h"
#include "stringconv.h"
#include "bytestream.h"

void WebSocket::connect(cstring target, arrayview<string> headers)
{
	inHandshake = true;
	gotFirstLine = false;
	
	HTTP::location loc;
	if (!HTTP::parseUrl(target, false, loc)) { cb_error(); return; }
	if (loc.proto == "wss") sock = socket::create_ssl(loc.domain, loc.port ? loc.port : 443, loop);
	if (loc.proto == "ws")  sock = socket::create(    loc.domain, loc.port ? loc.port : 80,  loop);
	sock->callback(bind_this(&WebSocket::activity), NULL);
	
	sock->send(string(
	           "GET "+loc.path+" HTTP/1.1\r\n"
	           "Host: "+loc.domain+"\r\n"
	           "Connection: upgrade\r\n"
	           "Upgrade: websocket\r\n"
	           //"Origin: "+loc.domain+"\r\n"
	           "Sec-WebSocket-Version: 13\r\n"
	           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" // TODO: de-hardcode this
	           ).bytes());
	
	for (cstring s : headers)
	{
		sock->send((s+"\r\n").bytes());
	}
	sock->send(cstring("\r\n").bytes());
}

void WebSocket::activity()
{
	uint8_t bytes[4096];
	int nbyte = sock->recv(bytes);
	if (nbyte < 0)
	{
//puts("SOCKDEAD");
		cancel();
		return;
	}
	msg += arrayview<byte>(bytes, nbyte);
	
//puts("TMP:"+tostringhex(msg));
	while (inHandshake)
	{
//puts("INSHK");
		size_t lf = msg.find('\n');
		if (lf == (size_t)-1) return;
		
		bool crlf = (lf > 0 && msg[lf-1]=='\r');
		cstring line = msg.slice(0, lf-crlf);
//puts("LINE:["+line+"]");
		if (!gotFirstLine)
		{
			if (!line.startswith("HTTP/1.1 101 ")) { cancel(); return; }
			gotFirstLine = true;
		}
		if (line == "")
		{
			inHandshake = false;
			sock->send(tosend.pull_buf());
			sock->send(tosend.pull_next());
			tosend.reset();
		}
		msg = msg.skip(lf+1);
	}
	
again:
	if (msg.size() < 2) return;
	
	uint8_t headsizespec = msg[1]&0x7F;
	uint8_t headsize = 2;
	if (msg[1] & 0x80) headsize += 4;
	if (headsizespec == 126) headsize += 2;
	if (headsizespec == 127) headsize += 8;
	
	if (msg.size() < headsize) return;
	
	size_t msgsize = headsize + headsizespec;
	if (headsizespec == 126) msgsize = headsize + bigend<uint16_t>(msg.slice(2, 2));
	if (headsizespec == 127) msgsize = headsize + bigend<uint64_t>(msg.slice(2, 8));
	
	if (msg.size() < msgsize) return;
	
	size_t bodysize = msgsize-headsize;
	
	arrayvieww<byte> out = msg.slice(headsize, bodysize);
	
	if (msg[1] & 0x80) // spec says server isn't allowed to mask, so this shouldn't happen
	{
		uint8_t key[4];
		key[0] = msg[headsize-4+0];
		key[1] = msg[headsize-4+1];
		key[2] = msg[headsize-4+2];
		key[3] = msg[headsize-4+3];
		for (size_t i=0;i<bodysize;i++)
		{
			out[i] ^= key[i&3];
		}
	}
	
	int type = (msg[0] & 0x0F);
//puts("RETURN:"+tostringhex(msg.slice(0,headsize))+" "+tostringhex(out));
	if (type == t_ping) send(msg, t_pong);
	
	if (type == t_text)
	{
		if (cb_str) cb_str(out);
		else cb_bin(out);
	}
	if (type == t_binary)
	{
		if (cb_bin) cb_bin(out);
		else cb_str(out);
	}
	
	if (!sock) // happens if cb_str doesn't like that input and resets websocket
	{
		return;
	}
	
	msg = msg.skip(msgsize);
	goto again;
}

void WebSocket::send(arrayview<byte> message, int type)
{
	if (!sock) return;
	
	bytestreamw header;
	header.u8(0x80 | type); // frame-FIN, opcode
	if (message.size() <= 125)
	{
		//apparently specially crafted websocket packets could confuse proxies and whatever
		//not a problem for me, my users aren't hostile (and that'd be the proxy's fault, not mine), not gonna mask properly
		//obvious follow up question: why is websocket on port 443 when it acts nothing like http
		header.u8(message.size() | 0x80);
	}
	else if (message.size() <= 65535)
	{
		header.u8(126 | 0x80);
		header.u16b(message.size());
	}
	else
	{
		header.u8(127 | 0x80);
		header.u64b(message.size());
	}
	header.u32b(0); // mask key
//puts("SEND:"+tostringhex(header.peek())+" "+tostringhex(message));
	if (inHandshake)
	{
		tosend.push(header.out(), message);
	}
	else
	{
		sock->send(header.out());
		sock->send(message);
	}
}

#include "test.h"
#ifdef ARLIB_TEST
test("WebSocket", "tcp,ssl,bytepipe", "websocket")
{
	test_skip("kinda slow");
	
	autoptr<runloop> loop = runloop::create();
	string str;
	function<void(string)> cb_str = bind_lambda([&](string str_inner){ loop->exit(); assert_eq(str, ""); str = str_inner; });
	function<void()> cb_error = bind_lambda([&](){ loop->exit(); assert(!"should be unreachable"); });
	
	function<string()> recvstr = bind_lambda([&]()->string { str = ""; loop->enter(); string ret = str; return ret; });
	
	function<bool()> timeout = bind_lambda([&]()->bool { assert_unreachable(); return false; });
	loop->set_timer_rel(10000, timeout);
	
	//this one is annoyingly slow, but I haven't found any alternatives
	//except wss://websocketstest.com/service, which just kicks me immediately
	WebSocket ws(loop);
	ws.callback(cb_str, cb_error);
	
	ws.connect("wss://echo.websocket.org");
	ws.send("hello");
	assert_eq(recvstr(), "hello");
	ws.send("hello");
	assert_eq(recvstr(), "hello");
	ws.send("hello");
	ws.send("hello");
	assert_eq(recvstr(), "hello");
	assert_eq(recvstr(), "hello");
	
#define msg128 "128bytes128bytes128bytes128bytes128bytes128bytes128bytes128bytes" \
               "128bytes128bytes128bytes128bytes128bytes128bytes128bytes128bytes"
	ws.send(msg128);
	assert_eq(recvstr(), msg128);
}
#endif
#endif
