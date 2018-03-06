#include "base64.h"

static const char encode[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t decode[128] = {
#define __ 128 // invalid
#define PA 129 // padding
//x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xA  xB  xC  xD  xE  xF
  __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00-0F */
  __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10-1F */
  __, __, __, __, __, __, __, __, __, __, __, 62, __, 62, __, 63, /* 20-2F */ // yes, there are two 62s and 63s
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, __, __, __, PA, __, __, /* 30-3F */
  __,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, /* 40-4F */
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, __, __, __, __, 63, /* 50-5F */
  __, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, /* 60-6F */
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, __, __, __, __, __, /* 70-7F */
};

//'out' must be 3 bytes; returns number of bytes actually written, 0 if input is invalid
static size_t base64_dec_core(uint8_t * out, const char * in)
{
	uint8_t c1 = in[0];
	uint8_t c2 = in[1];
	uint8_t c3 = in[2];
	uint8_t c4 = in[3];
	if ((c1|c2|c3|c4) >= 0x80) return 0;
	c1 = decode[c1];
	c2 = decode[c2];
	c3 = decode[c3];
	c4 = decode[c4];
	
	size_t ret = 3;
	if ((c1|c2|c3|c4) >= 0x80)
	{
		if (c4 == PA)
		{
			ret = 2;
			c4 = 0;
			if (c3 == PA)
			{
				ret = 1;
				c3 = 0;
			}
		}
		
		if ((c1|c2|c3|c4) >= 0x80) return 0;
	}
	
	//TODO: reject padded data where the bits of the partial byte aren't zero
	uint32_t outraw = (c1<<18) | (c2<<12) | (c3<<6) | (c4<<0);
	
	out[0] = outraw>>16;
	out[1] = outraw>>8;
	out[2] = outraw>>0;
	return ret;
}

static bool l_isspace(char ch)
{
	return (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
}

size_t base64_dec_raw(arrayvieww<byte> out, size_t* outend, cstring text, size_t* textend)
{
	size_t outat = 0;
	const char * ptr = (char*)text.bytes().ptr();
	const char * ptrend = ptr + text.length();
	
again:
	if (ptr < ptrend-4)
	{
		size_t n = base64_dec_core(out.skip(outat).ptr(), ptr);
		if (n != 3)
		{
			goto slowpath;
		}
		ptr += 4;
		outat += 3;
		goto again;
	}
	else
	{
	slowpath: ;
		char cs[4];
		
		int i = 0;
		while (i < 4)
		{
			if (ptr == ptrend) goto finish;
			char ch = *(ptr++);
			if (ch >= 0x21)
			{
				cs[i++] = ch;
				continue;
			}
			else
			{
				if (l_isspace(ch)) continue;
				goto finish;
			}
		}
		
		size_t n = base64_dec_core(out.skip(outat).ptr(), cs);
		outat += n;
		if (n != 3) goto finish;
		goto again;
	}
	
finish:
	while (ptr < ptrend && l_isspace(*ptr)) ptr++;
	
	if (outend) *outend = outat;
	if (textend) *textend = ptrend-ptr;
	
	if (ptr == ptrend) return outat;
	return 0;
}

array<byte> base64_dec(cstring text)
{
	array<byte> ret;
	ret.resize(base64_dec_len(text.length()));
	size_t actual = base64_dec_raw(ret, NULL, text, NULL);
	if (!actual) return NULL;
	ret.resize(actual);
	return ret;
}

//TODO
string base64_enc(arrayview<byte> bytes);

#include "test.h"
static void do_test(cstring enc, cstring dec)
{
	size_t declen_exp = dec.length();
	if (enc[enc.length()-1] == '=') declen_exp++;
	if (enc[enc.length()-2] == '=') declen_exp++;
	assert_eq(base64_dec_len(enc.length()), declen_exp);
	assert_eq(base64_enc_len(dec.length()), enc.length());
	//assert_eq(base64_enc(dec.bytes()), enc);
	assert_eq(cstring(base64_dec(enc)), dec);
}

test("base64", "string,array", "base64")
{
	testcall(do_test("cGxlYXN1cmUu", "pleasure."));
	testcall(do_test("bGVhc3VyZS4=", "leasure." ));
	testcall(do_test("ZWFzdXJlLg==", "easure."  ));
	testcall(do_test("YXN1cmUu",     "asure."   ));
	testcall(do_test("c3VyZS4=",     "sure."    ));
}
