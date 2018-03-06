#include "html.h"
#include "thread.h"
#include "test.h"

//string format:
//<ent-name> \0 <?> <bytes> <implicit \0>
//ent-name is entity name, like 'amp'
//? is ? if the semicolon is optional, blank if mandatory
//bytes is output

static const char * const entities_def[] = {
	u8"amp\0?\u0026",
	u8"apos\0\u0027",
	u8"gt\0\u003E",
	u8"lt\0\u003C",
	u8"quot\0?\"",
};

static const char * const * entities = entities_def;
static size_t n_entities = ARRAY_SIZE(entities_def);

void HTML::set_entities(const char * const * newents, size_t n)
{
	entities = newents;
	n_entities = n;
}

//text must NOT start with &
static bool entity_match(const char * ent, cstring text)
{
	size_t entlen = strlen(ent);
	if (!text.startswith(ent)) return false;
	return (text[entlen]==';' || ent[entlen+1]=='?');
}

//text must NOT start with &
static const char * find_entity(cstring text)
{
	for (size_t n=n_entities-1;n!=(size_t)-1;n--)
	{
		//TODO: binary search
		//if no match, try every shorter string
		//actually, maybe I should go for a hardcoded hashmap
		if (entity_match(entities[n], text)) return entities[n];
	}
	return NULL;
}

void HTML::entity_decode(string& out, cstring& in, bool isattr)
{
	//this follows the HTML5 spec <https://html.spec.whatwg.org/#character-reference-state>
	//12.2.5.72 Character reference state
	if (isalnum(in[1]))
	{
		//12.2.5.73 Named character reference state
		const char * ent = find_entity(in.substr(1, ~0));
		if (!ent) goto fail;
		size_t entlen = strlen(ent);
		size_t entlen_cons = 1+entlen;
		if (ent[entlen-1]!=';')
		{
			if (in[entlen_cons]==';') entlen_cons++;
			else
			{
				if (isattr &&                    // If the character reference was consumed as part of an attribute,
				    /* checked above */          // and the last character matched is not a U+003B SEMICOLON character (;),
				    (in[entlen_cons]=='=' ||     // and the next input character is either a U+003D EQUALS SIGN character (=) 
				      isalnum(in[entlen_cons]))) // or an ASCII alphanumeric,
				{                                // then, for historical reasons,
					goto fail;                     // flush code points consumed as a character reference and switch to the return state
				}
			}
		}
		in = in.substr(entlen_cons, ~0);
		const char * entval = (ent+entlen+1);
		if (*entval == '?') entval++;
		out += entval;
		return;
	}
	else if (in[1]=='#')
	{
		//12.2.5.75 Numeric character reference state
		size_t ccode;
		if (in[2]=='x' || in[2]=='X')
		{
			//12.2.5.76 Hexademical character reference start state
			size_t n = 3;
			while (isxdigit(in[n])) n++;
			if (n==3) goto fail;
			if (!fromstringhex(in.substr(3, n), ccode)) ccode = 0xFFFD;
			if (in[n]==';') n++;
			in = in.substr(n, ~0);
		}
		else
		{
			//12.2.5.77 Decimal character reference start state
			size_t n = 2;
			while (isdigit(in[n])) n++;
			if (n==2) goto fail;
			if (!fromstring(in.substr(2, n), ccode)) ccode = 0xFFFD;
			if (in[n]==';') n++;
			in = in.substr(n, ~0);
		}
		//12.2.5.80 Numeric character reference end state
		if (ccode == 0x00) ccode = 0xFFFD;
		if (ccode >  0x10FFFF) ccode = 0xFFFD;
		if (ccode >= 0xD800 && ccode <= 0xDFFF) ccode = 0xFFFD;
		if (ccode >= 0x80 && ccode <= 0x9F)
		{
			uint32_t newcp = string::cpfromwindows1252(ccode); // yes, this is in the spec
			if (newcp != 0xFFFD) ccode = newcp;
		}
		out += string::codepoint(ccode);
		return;
	}
	abort(); // should be unreachable
	
fail:
	out += '&';
	in = in.substr(1, ~0);
}

test("HTML entities", "string", "html")
{
	//do not use existing entities other than amp apos gt lt quot,
	// or this will fail depending on whether it runs before or after htmlent.cpp
	//(nonexistent entities are fine)
	assert_eq(HTML::entity_decode("&quot;&#xF8;&#248;&#xF8&#&#x"), "\"øøø&#&#x");
	assert_eq(HTML::entity_decode("&quot&#xF8&#248&#xF8&#&#x"), "\"øøø&#&#x");
	assert_eq(HTML::entity_decode("foo&quot&quote=42&quot=42"), "foo\"\"e=42\"=42");
	assert_eq(HTML::entity_decode("foo&quot&quote=42&quot=42", true), "foo\"&quote=42&quot=42");
	assert_eq(HTML::entity_decode("&foo;"), "&foo;");
	
	assert_eq(HTML::entity_decode("&amp"), "&");
	assert_eq(HTML::entity_decode("&amp;"), "&");
	
	assert_eq(HTML::entity_decode("&ampersand"), "&ersand");
	assert_eq(HTML::entity_decode("&ampersand;"), "&ersand;");
	
	assert_eq(HTML::entity_decode("&#x00;&#x01;&#x7F;&#x80;&#x81;&#xD800;&#xFFFFFFFF;"), "�\x01\x7F€\xC2\x81��");
}
