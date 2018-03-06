#pragma once
#include "global.h"
#include "string.h"

class HTML {
	//At entry, 'in' must start with an ampersand.
	//At exit, 'out' will be extended, and 'in' will become a (possibly empty) substring of itself.
	static void entity_decode(string& out, cstring& in, bool isattr);
	
	static void set_entities(const char * const * newents, size_t n);
	
public:
	//TODO: proper parser
	static string entity_decode(cstring in, bool isattr = false)
	{
		string out;
		while (in)
		{
			if (in[0]=='&') entity_decode(out, in, isattr);
			else
			{
				out += in[0];
				in = in.substr(1, ~0);
			}
		}
		return out;
	}
	
	//Enables all HTML entities, not just &amp; &apos; &gt; &lt; &quot;
	//Affects every future HTML parser. Increases program size by approximately 25KB.
	static void all_entities();
};
