#pragma once
#include "array.h"
#include "string.h"

//A bytepipe accepts an infinite amount of bytes and returns them, first one first.
//Guaranteed amortized O(n) no matter how many bytes are pushed at the time, except if pull_line() is used and there is no line.
class bytepipe {
	array<byte> buf1;
	size_t buf1st;
	size_t buf1end;
	
	array<byte> buf2;
	size_t buf2end;
	
	void try_swap()
	{
		if (buf1st == buf1end)
		{
			buf1.swap(buf2);
			buf1st = 0;
			buf1end = buf2end;
			buf2end = 0;
			if (buf2.size() > 65536) buf2.resize(65536);
		}
	}
	
	void push_one(arrayview<byte> bytes)
	{
		arrayvieww<byte> tmp = push_buf(bytes.size());
		memcpy(tmp.ptr(), bytes.ptr(), bytes.size());
		push_done(bytes.size());
	}
	void push_one(cstring str)
	{
		push(str.bytes());
	}
	void push() {}
	
public:
	bytepipe()
	{
		reset();
	}
	
	//Will return a buffer of at least 'bytes' bytes. Can be bigger. Use push_done afterwards.
	arrayvieww<byte> push_buf(size_t bytes = 512)
	{
		if (buf2end + bytes > buf2.size())
		{
			try_swap();
		}
		if (buf2end + bytes > buf2.size())
		{
			size_t newsize = buf2.size();
			while (buf2end + bytes > newsize) newsize *= 2;
			buf2.resize(newsize);
		}
		return buf2.skip(buf2end);
	}
	void push_done(size_t bytes)
	{
		buf2end += bytes;
	}
	
	template<typename T, typename... Tnext> void push(T first, Tnext... next)
	{
		push_one(first);
		push(next...);
	}
	
	//Can return less than remaining().
	arrayview<byte> pull_buf()
	{
		try_swap();
		return buf1.slice(buf1st, buf1end-buf1st);
	}
	//Returns whatever was pushed that pull_buf didn't return. Can't be acknowledged and discarded, use pull_buf.
	arrayview<byte> pull_next()
	{
		return buf2.slice(0, buf2end);
	}
	void pull_done(size_t bytes)
	{
		buf1st += bytes;
	}
	void pull_done(arrayview<byte> bytes)
	{
		pull_done(bytes.size());
	}
	//Returns the entire thing.
	arrayview<byte> pull_buf_full()
	{
		if (buf1end+buf2end > buf1.size())
		{
			if (buf1st != 0)
			{
				memmove(buf1.ptr(), buf1.skip(buf1st).ptr(), buf1end-buf1st);
				buf1end -= buf1st;
				buf1st = 0;
			}
			if (buf1end+buf2end > buf1.size())
			{
				if (buf1end > buf2end) buf1.resize(buf1.size()*2);
				else buf1.resize(buf2.size()*2);
			}
		}
		
		memcpy(buf1.slice(buf1end, buf2end).ptr(), buf2.ptr(), buf2end);
		buf1end += buf2end;
		buf2end = 0;
		
		return buf1.slice(buf1st, buf1end-buf1st);
	}
	//Returns the entire thing, and immediately acknowledges it. Other than the return value, it's equivalent to reset().
	array<byte> pull_buf_full_drain()
	{
		if (buf1st != 0)
		{
			memmove(buf1.skip(buf1st).ptr(), buf1.ptr(), buf1end-buf1st);
			buf1end -= buf1st;
		}
		if (buf2end != 0)
		{
			if (buf1end+buf2end > buf1.size())
			{
				size_t newsize = buf1.size();
				while (buf1end+buf2end > newsize) newsize *= 2;
				buf1.resize(newsize);
			}
			memcpy(buf1.skip(buf1end).ptr(), buf2.ptr(), buf2end);
			buf1end += buf2end;
		}
		
		array<byte> ret = std::move(buf1);
		ret.resize(buf1end);
		
		reset();
		return ret;
	}
	
	//Returns data until and including the next \n. Doesn't acknowledge it. If there is no \n, returns an empty array.
	arrayview<byte> pull_line()
	{
		byte* start = buf1.ptr()+buf1st;
		size_t len = buf1end-buf1st;
		byte* nl = (byte*)memchr(start, '\n', len);
		if (nl)
		{
			return arrayview<byte>(start, nl+1-start);
		}
		
		nl = (byte*)memchr(buf2.ptr(), '\n', buf2end);
		if (nl)
		{
			size_t pos = buf1end-buf1st + nl+1-buf2.ptr();
			return pull_buf_full().slice(0, pos);
		}
		
		return NULL;
	}
	//Returns 'line' minus a trailing \r\n or \n. The \n must exist. Usable together with the above.
	static arrayview<byte> trim_line(arrayview<byte> line)
	{
		if (line.size()==1) return NULL;
		if (line[line.size()-2]=='\r') return line.slice(0, line.size()-2);
		else return line.slice(0, line.size()-1);
	}
	
	size_t remaining() { return buf1end-buf1st+buf2end; }
	void reset()
	{
		buf1.resize(1024);
		buf1st = 0;
		buf1end = 0;
		buf2.resize(1024);
		buf2end = 0;
	}
};
