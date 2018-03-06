#pragma once
#ifdef ARLIB_SOCKET
#include "global.h"
#include "containers.h"
#include "socket.h"
#include "bytepipe.h"

class HTTP : nocopy {
public:
	HTTP(runloop* loop) : loop(loop) {}
	
	struct req {
		//Every field except 'url' is optional.
		string url;
		
		string method;
		//These headers are added automatically, if not already present:
		//Connection: keep-alive
		//Host: <from url>
		//Content-Length: <from postdata> (if not GET)
		//Content-Type: application/x-www-form-urlencoded
		//           or application/json if postdata starts with [ or {
		array<string> headers; // TODO: multimap
		array<byte> postdata;
		
		uintptr_t id; // Passed unchanged in the rsp object, and used for cancel(). Otherwise not used.
		
		//If the server sends this much data, or hasn't finished in the given time, fail.
		//They're pretty approximate; a request may succeed if the server sends slightly more than this.
		uint64_t limit_ms = 5000;
		size_t limit_bytes = 1048576;
		
		req() {}
		req(string url) : url(url) {}
	};
	
	struct rsp {
		req q;
		
		bool success;
		
		enum {
			e_bad_url       = -1, // couldn't parse URL
			e_different_url = -2, // can't use Keep-Alive between these, create a new http object
			e_connect       = -3, // couldn't open TCP/SSL stream
			e_broken        = -4, // server unexpectedly closed connection, or timeout
			e_not_http      = -5, // the server isn't speaking HTTP
			e_canceled      = -6, // request was canceled; used only internally, callback is never called with this reason
			e_timeout       = -7, // limit_ms was reached
			e_too_big       = -8, // limit_bytes was reached
			//may also be a normal http status code (200, 302, 404, etc)
		};
		int status = 0;
		//string status_str; // useless
		
		array<string> headers; // TODO: switch to multimap once it exists
		array<byte> body;
		
		
		operator arrayvieww<byte>()
		{
			if (status >= 200 && status <= 299) return body;
			else return NULL;
		}
		//operator string() { return body; } // throws ambiguity errors if enabled
		
		cstring header(cstring name) const
		{
			for (cstring head : headers)
			{
				if (head.istartswith(name) && head[name.length()]==':' && head[name.length()+1]==' ')
				{
					return head.substr(name.length()+2, ~0);
				}
			}
			return "";
		}
		
		cstring text() const { return body; }
	};
	
private:
	struct rsp_i {
		rsp r;
		function<void(rsp)> callback;
	};
public:
	
	//Multiple requests may be sent to the same object. This will make them use HTTP Keep-Alive.
	//The requests must be to the same protocol-domain-port tuple.
	//Failures are reported in the callback.
	//Callbacks will be called in an arbitrary order. If there's more than one request, use different callbacks or vary the ID.
	void send(req q, function<void(rsp)> callback);
	//If the HTTP object is currently trying to send a request with this ID, it's canceled.
	//Callback won't be called, and unless request has already been sent, it won't be.
	//If multiple have that ID, undefined which of them is canceled. Returns whether anything happened.
	bool cancel(uintptr_t id);
	
	//Discards any unfinished requests, errors, and similar.
	void reset()
	{
		lasthost = location();
		requests.reset();
		next_send = 0;
		sock = NULL;
		state = st_boundary;
	}
	
	
	struct location {
		string proto;
		string domain;
		int port;
		string path;
	};
	static bool parseUrl(cstring url, bool relative, location& out);
	
	~HTTP();
	
private:
	void resolve(bool* deleted, size_t id, bool success);
	void resolve_err_v(bool* deleted, size_t id, int err)
	{
		requests[id].r.status = err;
		resolve(deleted, id, false);
	}
	bool resolve_err_f(bool* deleted, size_t id, int err) { resolve_err_v(deleted, id, err); return false; }
	
	void sock_cancel() { sock = NULL; }
	
	//if tosend is empty, adds requests[active_req] to tosend, then increments active_req; if not empty, does nothing
	//also sets this->location, if not set already
	void try_compile_req();
	
	void create_sock();
	void activity();
	
	
	location lasthost; // used to verify that the requests aren't sent anywhere they don't belong
	array<rsp_i> requests;
	size_t next_send = 0; // index to requests[] next to sock->send(), or requests.size() if all done / in tosend
	
	runloop* loop;
	autoptr<socket> sock;
	
	bool do_timeout();
	uintptr_t timeout_id = 0;
	
	size_t bytes_in_req;
	void reset_limits();
	
	bool* deleted_p = NULL;
	
	enum {
		st_boundary, // between requests; if socket closes, make a new one
		st_boundary_retried, // between requests; if socket closes, abort request
		
		st_status, // waiting for HTTP/1.1 200 OK
		st_header, // waiting for header, or \r\n\r\n
		st_body, // waiting for additional bytes, non-chunked
		st_body_chunk_len, // waiting for chunk length
		st_body_chunk, // waiting for chunk
		st_body_chunk_term, // waiting for final \r\n in chunk
		st_body_chunk_term_final, // waiting for final \r\n in terminating 0\r\n\r\n chunk
	} state = st_boundary;
	string fragment;
	size_t bytesleft;
};
#endif
