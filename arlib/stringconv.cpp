#include "stringconv.h"
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "test.h"

#define FROMFUNC(t,frt,f) \
	bool fromstring(cstring s, t& out) \
	{ \
		out = 0; \
		if (!s || isspace(s[0])) return false; \
		string tmp_s = s; /* copy in case 's' isn't terminated */ \
		const char * tmp_cp = tmp_s; \
		char * tmp_cpo; \
		frt ret = f(tmp_cp, &tmp_cpo, 10); \
		if (tmp_cpo != tmp_cp + s.length()) return false; \
		if ((t)ret != (frt)ret) return false; \
		out = ret; \
		return true; \
	}

//specification: if the input is a hex number, return something strtoul accepts
//otherwise, return something that strtoul rejects
//this means replace 0x with x, if present
static const char * drop0x(const char * in)
{
	if (in[0]=='0' && (in[1]=='x' || in[1]=='X')) return in+1;
	else return in;
}

#define FROMFUNCHEX(t,frt,f) \
	FROMFUNC(t,frt,f) \
	bool fromstringhex(cstring s, t& out) \
	{ \
		out = 0; \
		string tmp_s = s; \
		const char * tmp_cp = tmp_s; \
		if (!*tmp_cp || isspace(*tmp_cp)) return false; \
		char * tmp_cpo; \
		frt ret = f(drop0x(tmp_cp), &tmp_cpo, 16); \
		if (tmp_cpo != tmp_cp + s.length()) return false; \
		if ((t)ret != (frt)ret) return false; \
		out = ret; \
		return true; \
	}

FROMFUNC(     signed char,    signed long, strtol)
FROMFUNCHEX(unsigned char,  unsigned long, strtoul)
FROMFUNC(     signed short,   signed long, strtol)
FROMFUNCHEX(unsigned short, unsigned long, strtoul)
FROMFUNC(     signed int,     signed long, strtol)
FROMFUNCHEX(unsigned int,   unsigned long, strtoul)
FROMFUNC(     signed long,    signed long, strtol)
FROMFUNCHEX(unsigned long,  unsigned long, strtoul)
FROMFUNC(     signed long long,   signed long long, strtoll)
FROMFUNCHEX(unsigned long long, unsigned long long, strtoull)

bool fromstring(cstring s, double& out)
{
	out = 0;
	if (!isdigit(s[0])) return false;
	string tmp_s = s;
	const char * tmp_cp = tmp_s;
	char * tmp_cpo;
	double ret = strtod(drop0x(tmp_cp), &tmp_cpo);
	if (tmp_cpo != tmp_cp + s.length()) return false;
	if (!isdigit(tmp_cpo[-1])) return false;
	if (ret==HUGE_VAL || ret==-HUGE_VAL) return false;
	out = ret;
	return true;
}

//strtof exists in C99, but let's not use that
bool fromstring(cstring s, float& out)
{
	out = 0;
	double tmp;
	if (!fromstring(s, tmp)) return false;
	if (tmp < -FLT_MAX || tmp > FLT_MAX) return false;
	out = tmp;
	return true;
}

bool fromstring(cstring s, bool& out)
{
	if (s=="false" || s=="0")
	{
		out = false;
		return true;
	}
	
	if (s=="true" || s=="1")
	{
		out = true;
		return true;
	}
	
	out = false;
	return false;
}


string tostringhex(arrayview<byte> val)
{
	string ret;
	arrayvieww<byte> retb = ret.construct(val.size()*2);
	for (size_t i=0;i<val.size();i++)
	{
		sprintf((char*)retb.slice(i*2, 2).ptr(), "%.2X", val[i]);
	}
	return ret;
}

bool fromstringhex(cstring s, arrayvieww<byte> val)
{
	if (val.size()*2 != s.length()) return false;
	bool ok = true;
	for (size_t i=0;i<val.size();i++)
	{
		ok &= fromstringhex(s.substr(i*2, i*2+2), val[i]);
	}
	return ok;
}
bool fromstringhex(cstring s, array<byte>& val)
{
	val.resize(s.length()/2);
	return fromstringhex(s, (arrayvieww<byte>)val);
}


template<typename T> void testunhex(const char * S, T V)
{
	T a;
	assert_eq(fromstringhex(S, a), true);
	assert_eq(a, V);
}
template<typename T> void testundec(const char * S, T V)
{
	T a;
	assert_eq(fromstring(S, a), true);
	assert_eq(a, V);
}
test("string conversion", "", "string")
{
	testcall(testunhex<unsigned char     >("aa", 0xaa));
	testcall(testunhex<unsigned char     >("AA", 0xAA));
	testcall(testunhex<unsigned short    >("aaaa", 0xaaaa));
	testcall(testunhex<unsigned short    >("AAAA", 0xAAAA)); // AAAAAAAAAAAHHHHH MOTHERLAND http://www.albinoblacksheep.com/flash/end
	testcall(testunhex<unsigned int      >("aaaaaaaa", 0xaaaaaaaa));
	testcall(testunhex<unsigned int      >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long     >("aaaaaaaa", 0xaaaaaaaa)); // this is sometimes 64bit, but good enough
	testcall(testunhex<unsigned long     >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long long>("aaaaaaaaaaaaaaaa", 0xaaaaaaaaaaaaaaaa));
	testcall(testunhex<unsigned long long>("AAAAAAAAAAAAAAAA", 0xAAAAAAAAAAAAAAAA));
	
	testcall(testundec<int>("123", 123));
	testcall(testundec<int>("0123", 123));
	testcall(testundec<int>("00123", 123));
	testcall(testundec<int>("000123", 123));
	testcall(testundec<int>("0", 0));
	testcall(testundec<double>("123", 123));
	testcall(testundec<double>("0123", 123));
	testcall(testundec<double>("00123", 123));
	testcall(testundec<double>("000123", 123));
	testcall(testundec<double>("0", 0));
	testcall(testundec<double>("0e1", 0)); // this input has triggered the 0x detector, making it fail
	testcall(testundec<double>("0e-1", 0));
	testcall(testundec<double>("0e+1", 0));
	testcall(testundec<double>("11e1", 110));
	testcall(testundec<double>("11e+1", 110));
	testcall(testundec<float>("2.5", 2.5));
	testcall(testundec<float>("2.5e+1", 25));
	
	byte foo[4] = {0x12,0x34,0x56,0x78};
	assert_eq(tostringhex(arrayview<byte>(foo)), "12345678");
	
	assert(fromstringhex("87654321", arrayvieww<byte>(foo)));
	assert_eq(foo[0], 0x87); assert_eq(foo[1], 0x65); assert_eq(foo[2], 0x43); assert_eq(foo[3], 0x21);
	
	array<byte> bar;
	assert(fromstringhex("1234567890", bar));
	assert_eq(bar.size(), 5);
	assert_eq(bar[0], 0x12);
	assert_eq(bar[1], 0x34);
	assert_eq(bar[2], 0x56);
	assert_eq(bar[3], 0x78);
	assert_eq(bar[4], 0x90);
	
	assert(!fromstringhex("123456", arrayvieww<byte>(foo))); // not 4 bytes
	assert(!fromstringhex("1234567", bar)); // odd length
	assert(!fromstringhex("0x123456", bar)); // no 0x allowed
	
	uint32_t u;
	float f;
	assert(!fromstring("", u)); // this isn't an integer
	assert(!fromstringhex("", u));
	assert(!fromstring("", f));
	
	assert(!fromstring("2,5", f)); // this is not the decimal separator, has never been and will never be
	
	string s;
	s += '7';
	s += '\0';
	assert(!fromstring(s, u)); // no nul allowed
	assert(!fromstring(s, f));
	assert(!fromstringhex(s, u));
	
	assert(!fromstring(" 42", u));
	assert(!fromstring("0x42", u));
	assert(!fromstringhex(" 42", u));
	assert(!fromstringhex("0x42", u));
	assert(!fromstring(" 42", f));
	assert(!fromstring("0x42", f));
	
	assert(!fromstring("1e", f));
	assert(!fromstring("1e+", f));
	assert(!fromstring("1e-", f));
	assert(!fromstring("1.", f));
	assert(!fromstring(".1", f));
}
