#pragma once
#include "array.h"
#include "endian.h"

//prefers little endian, it's more common
//you're welcome to extend this object if you need a more rare operation, like leb128
class bytestream {
protected:
	const uint8_t* start;
	const uint8_t* at;
	const uint8_t* end;
	
public:
	bytestream(arrayview<uint8_t> buf) : start(buf.ptr()), at(buf.ptr()), end(buf.ptr()+buf.size()) {}
	bytestream(const bytestream& other) : start(other.start), at(other.at), end(other.at) {}
	bytestream() : start(NULL), at(NULL), end(NULL) {}
	
	arrayview<uint8_t> bytes(size_t n)
	{
		arrayview<uint8_t> ret = arrayview<uint8_t>(at, n);
		at += n;
		return ret;
	}
	arrayview<uint8_t> peekbytes(size_t n)
	{
		return arrayview<uint8_t>(at, n);
	}
	bool signature(cstring sig)
	{
		if (remaining() < sig.length()) return false;
		
		arrayview<uint8_t> expected = sig.bytes();
		arrayview<uint8_t> actual = peekbytes(sig.length());
		if (actual == expected)
		{
			bytes(sig.length());
			return true;
		}
		else return false;
	}
	uint8_t u8()
	{
		return *(at++);
	}
	int u8_or(int otherwise)
	{
		if (at==end) return otherwise;
		return *(at++);
	}
	uint16_t u16l()
	{
		return end_nat_to_le(bytes(2).reinterpret<uint16_t>()[0]);
	}
	uint16_t u16b()
	{
		return end_nat_to_be(bytes(2).reinterpret<uint16_t>()[0]);
	}
	uint32_t u32l()
	{
		return end_nat_to_le(bytes(4).reinterpret<uint32_t>()[0]);
	}
	uint32_t u32b()
	{
		return end_nat_to_be(bytes(4).reinterpret<uint32_t>()[0]);
	}
	uint32_t u32lat(size_t pos)
	{
		return end_nat_to_le(*(uint32_t*)(start+pos));
	}
	uint32_t u32bat(size_t pos)
	{
		return end_nat_to_be(*(uint32_t*)(start+pos));
	}
	
	size_t tell() { return at-start; }
	size_t size() { return end-start; }
	size_t remaining() { return end-at; }
	
	void seek(size_t pos) { at = start+pos; }
	
	uint32_t u24l()
	{
		//doubt this is worth optimizing, probably rare...
		arrayview<uint8_t> b = bytes(3);
		return b[0] | b[1]<<8 | b[2]<<16;
	}
	uint32_t u24b()
	{
		return end_swap24(u24l());
	}
};

class bytestreamw {
protected:
	array<byte> buf;
	
public:
	void bytes(arrayview<byte> data)
	{
		buf += data;
	}
	void text(cstring str)
	{
		buf += str.bytes();
	}
	void u8(uint8_t val)
	{
		buf += arrayview<byte>(&val, 1);
	}
	void u16l(uint16_t val)
	{
		litend<uint16_t> valn = val;
		buf += valn.bytes();
	}
	void u16b(uint16_t val)
	{
		bigend<uint16_t> valn = val;
		buf += valn.bytes();
	}
	void u24l(uint32_t val)
	{
		u8(val>>0);
		u8(val>>8);
		u8(val>>16);
	}
	void u24b(uint32_t val)
	{
		u8(val>>16);
		u8(val>>8);
		u8(val>>0);
	}
	void u32l(uint32_t val)
	{
		litend<uint32_t> valn = val;
		buf += valn.bytes();
	}
	void u32b(uint32_t val)
	{
		bigend<uint32_t> valn = val;
		buf += valn.bytes();
	}
	void u64l(uint64_t val)
	{
		litend<uint64_t> valn = val;
		buf += valn.bytes();
	}
	void u64b(uint64_t val)
	{
		bigend<uint64_t> valn = val;
		buf += valn.bytes();
	}
	
	arrayview<byte> peek()
	{
		return buf;
	}
	array<byte> out()
	{
		return std::move(buf);
	}
};
