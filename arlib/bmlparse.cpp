#include "bml.h"
#include "test.h"
#include <ctype.h>

/*

[bml]
a
b
 c
d e f
g
 :h
 :i
j
 k
  l
  m

read "a"
{a}
set m_indent_step[0]
return enter a

read "b"
m_indent_step.size(){1} > m_indent.length(){0}, so:
 {b}
 clear last true element of m_indent_step
 clear trailing false elements of m_indent_step
 restore "b" to read buffer
 return exit

read "b", goto {a}

read " c"
{c}
set m_indent to " "
m_indent_step.size(){1} <= m_indent.length(){1}
set m_indent_step[1]
return enter c

read "d e"
set m_indent to ""
m_indent_step.size(){2} > m_indent.length(){0}, so goto {b}

read "d e f"
m_indent_step.size(){1} > m_indent.length(){0}, so goto {b}

read "d e f"
m_indent_step.size(){0} <= m_indent.length(){0}
set m_inlines = " e f" (or "e f", not sure and doesn't matter)
goto {a}

m_inlines is not empty, so:
{e}
read "e f"
set m_exit
set m_inlines to "f"
return enter e

m_exit is set, so:
{ex}
clear m_exit
return exit

m_inlines is not empty, so goto {e} [m_inlines = ""]
m_exit is set, so goto {ex}


[bml]
g
 :h
 :i
j
 k
  l
  m

read "g"
read " :h"
if it doesn't start with colon, restore to read buffer
but it does, so:
  set m_indent = " "
  read " :i"
  it too starts with colon, so ensure that indentation is identical
  read "j"
  it does not start with colon, so restore to read buffer
  set m_indent_step[0]
  return enter g="h i"

read "j", goto {a}
read " k", goto {c}

read "  l"
set m_indent to "  "
m_indent_step.size(){2} <= m_indent.length(){2}
set m_indent_step[2]
return enter l

read "  m"
m_indent_step.size(){3} > m_indent.length(){2}, so goto {b}

read "  m"
m_indent_step.size(){2} <= m_indent.length(){2}
set m_indent_step[2]
return enter m

read ""
set m_indent to ""
m_indent_step.size(){2} > m_indent.length(){0}, so goto {b}

read ""
m_indent_step.size(){1} > m_indent.length(){0}, so goto {b}

read ""
nothing else to do, so return finish



process in this order:

process m_exit
process m_inlines

read line
check for consistent whitespace (set m_indent even on failure)
set m_indent
check if m_indent_step.size requires exit
extract main node on line; put remainder in m_inlines
if no value, peek next line
 as long as it starts with colon, discard line and peek next
 set value to that
set m_indent_step
return enter

'read line' must discard blank/comment lines
if there is no line, return finish

*/

static cstring cut(cstring& input, int skipstart, int cut, int skipafter)
{
	cstring ret = input.substr(skipstart, cut);
	input = input.substr(cut+skipafter, ~0);
	return ret;
}

//returns size of leading whitespace and comments
static size_t bml_size_white(const cstring& data)
{
	int i = 0;
	while (data[i]==' ' || data[i]=='\t') i++;
	if (data[i]=='#' || (data[i]=='/' && data[i+1]=='/')) return data.length();
	else return i;
}

//takes a single line, returns the first node in it
//hasvalue is to differentiate 'foo' from 'foo='; only the former allows a multi-line value
//returns true if found a node, false for error or nothing
//if error, 'value' is the error message; if line was blank, 'value' is ""
static bool bml_parse_inline_node(cstring& data, cstring& node, bool& hasvalue, cstring& value)
{
	size_t nodestart = bml_size_white(data);
	if (nodestart == data.length())
	{
		data = "";
		value = "";
		return false;
	}
	
	size_t nodelen = nodestart;
	while (isalnum(data[nodelen]) || data[nodelen]=='-' || data[nodelen]=='.') nodelen++;
	if (nodestart == nodelen)
	{
		value = "Invalid node name";
		while (data[nodelen]!='\n' && data[nodelen]!='\0') nodelen++;
		data = data.substr(nodelen, ~0);
		return false;
	}
	node = cut(data, nodestart, nodelen, 0);
	switch (data[0])
	{
		case '\0':
		case '\t':
		case ' ':
		{
			hasvalue = false;
			return true;
		}
		case ':':
		{
			hasvalue = true;
			int valstart = 1;
			while (data[valstart]==' ' || data[valstart]=='\t') valstart++;
			value = data.substr(valstart, ~0);
			data = "";
			return true;
		}
		case '=':
		{
			if (data[1]=='"')
			{
				hasvalue = true;
				int valend = 2;
				while (data[valend]!='"' && data[valend]!='\0') valend++;
				if (data[valend]!='"' || !strchr(" \t", data[valend+1]))
				{
					while (data[valend]!='\0') valend++;
					data = data.substr(valend, ~0);
					value = "Broken quoted value";
					return false;
				}
				value = cut(data, 2, valend, 1);
				return true;
			}
			else
			{
				hasvalue = true;
				int valend = 0;
				while (data[valend]!=' ' && data[valend]!='"' && data[valend]!='\0') valend++;
				if (data[valend]=='"')
				{
					while (data[valend]!='\0') valend++;
					data = data.substr(valend, ~0);
					value = "Broken quoted value";
					return false;
				}
				value = cut(data, 1, valend, 0);
				return true;
			}
		}
		default:
			value = "Invalid node suffix";
			return false;
	}
}

static bool isendl(char ch)
{
	//this 32 is a perf hack
	if (ch>=32) return false;
	return (ch=='\r' || ch=='\n' || ch=='\0');
}

static size_t linelen(const cstring& input)
{
	//pointers are generally a bad idea, but this is such a hotspot it's worth it
	const uint8_t * inputraw = input.bytes().ptr();
	size_t nlpos = 0;
	if (input.bytes_hasterm())
	{
		while (!isendl(inputraw[nlpos])) nlpos++;
	}
	else
	{
		size_t inputlen = input.length();
		while (nlpos < inputlen && !isendl(inputraw[nlpos])) nlpos++;
	}
	return nlpos;
}

static cstring cutline(cstring& input)
{
	size_t nlpos = linelen(input);
	return cut(input, 0, nlpos, (input[nlpos]=='\r') ? 2 : (input[nlpos]=='\n') ? 1 : 0);
}

inline bool bmlparser::getline(bool allow_empty)
{
nextline:
	if (!m_data)
	{
null:
		m_thisline = "";
		m_indent = "";
		return true;
	}
	
	m_thisline = cutline(m_data);
	size_t indentlen = bml_size_white(m_thisline);
	if (indentlen == m_thisline.length())
	{
		if (allow_empty) goto nextline;
		else goto null;
	}
	
	int sharedindent = min(indentlen, m_indent.length());
	bool badwhite = (memcmp(m_thisline.bytes().ptr(), m_indent.bytes().ptr(), sharedindent)!=0);
	
	m_indent = cut(m_thisline, 0, indentlen, 0);
	
	return !badwhite;
}

bmlparser::event bmlparser::next()
{
	if (m_exit)
	{
		m_exit = false;
		return event(exit);
	}
	
	if (m_inlines)
	{
		event ev = { enter };
		bool dummy;
		if (bml_parse_inline_node(m_inlines, ev.name, dummy, ev.value))
		{
			m_exit = true;
			return ev;
		}
		else if (ev.value) return event(error, "", ev.value);
		//else fall through
	}
	
	if (!m_thisline && m_data)
	{
		if (!getline(true)) return event(error, "", "Mixed tabs and spaces");
	}
	
	if (m_indent_step.size() > m_indent.length())
	{
	handle_indent:
		if (!m_indent_step.get_or(m_indent.length(), false))
		{
			//this may throw random mix-tab-space errors that weren't present in the original,
			// but only if the document contains mix-tab-space and this error already.
			if (m_indent_step.size() > m_indent.length()) m_indent += m_indent[0];
			else m_indent = m_indent.substr(0, ~1);
			return event(error, "", "Invalid indentation depth");
		}
		
		int lasttrue = m_indent_step.size()-2; // -1 for [size()] being OOB, -1 to skip the true at [size()-1] and discard it
		while (lasttrue>=0 && m_indent_step[lasttrue]==false) lasttrue--;
		
		m_indent_step.resize(lasttrue+1);
		return event(exit);
	}
	
	if (!m_thisline)
	{
		if (m_indent_step.size()) goto handle_indent;
		return event(finish);
	}
	
	m_inlines = m_thisline;
	m_thisline = "";
	
	cstring node;
	bool hasvalue;
	cstring value;
	if (!bml_parse_inline_node(m_inlines, node, hasvalue, value))
	{
		//m_inlines can't be empty here, we've called getline()
		return event(error, "", value);
	}
	
	size_t indentlen = m_indent.length(); // changed by getline
	//multilines
	if (!hasvalue)
	{
		if (!getline(false)) return event(error, "", "Mixed tabs and spaces");
		if (m_thisline[0] == ':')
		{
			size_t inner_indent = m_indent.length();
			m_tmp_value = m_thisline.substr(1, ~0);
			if (!getline(false)) return event(error, "", "Mixed tabs and spaces");
			while (m_thisline[0] == ':')
			{
				if (inner_indent != m_indent.length()) return event(error, "", "Multi-line values must have constant indentation");
				m_tmp_value += "\n" + m_thisline.substr(1, ~0);
				if (!getline(false)) return event(error, "", "Mixed tabs and spaces");
			}
			
			if (m_indent.length() != inner_indent)
			{
				if (m_indent.length() > inner_indent)
				{
					return event(error, "", "Can't change indentation after a multi-line value");
				}
				if (m_indent.length() > indentlen && !m_indent_step.get_or(m_indent.length(), false))
				{
					return event(error, "", "Invalid indentation depth");
				}
			}
			
			value = m_tmp_value;
		}
	}
	
	m_indent_step.set_resize(indentlen, true);
	return event(enter, node, value);
}


#ifdef ARLIB_TEST
#define e_enter bmlparser::enter
#define e_exit bmlparser::exit
#define e_error bmlparser::error
#define e_finish bmlparser::finish
#define e_extract -1

static const char * test1 =
"node\n"
"node=foo\n"
"node=\"foo bar\"\n"
"node: foo bar\n"
"node\n"
" child\n"
"node child=foo\n"
"node=\n"
"node=\"\"\n"
"node:\n"
"node\tchild\n"
"node   \n"
"#bar\n"
"node\n"
"  :data\n"
"node\n"
"  child\n"
"    :data\n"
"node\n"
"  child\n"
"    :data\n"
"  child\n"
"node:\n"
"node: \n"
"node\n"
"  :\n"
"node";

static bmlparser::event test1e[]={
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node", "foo" },
	{ e_exit },
	{ e_enter, "node", "foo bar" },
	{ e_exit },
	{ e_enter, "node", "foo bar" },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child", "foo" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node", "data" },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child", "data" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child", "data" },
		{ e_exit },
		{ e_enter, "child" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_finish }
};

static const char * test2 =
"parent\n"
" node=123 child1=456 child2: 789 123\n"
"  child3\n"
;
static bmlparser::event test2e[]={
	{ e_enter, "parent" },
		{ e_enter, "node", "123" },
			{ e_enter, "child1", "456" },
			{ e_exit },
			{ e_enter, "child2", "789 123" },
			{ e_exit },
			{ e_enter, "child3" },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_finish }
};

static const char * test3 =
"a b=1 c=2 d: 3\n"
" e=4 f=5\n"
" g h=6\n"
"  :7\n"
"  :8\n"
"i";
static bmlparser::event test3e[]={
	{ e_enter, "a" },
		{ e_enter, "b", "1" },
		{ e_exit },
		{ e_enter, "c", "2" },
		{ e_exit },
		{ e_enter, "d", "3" },
		{ e_exit },
		{ e_enter, "e", "4" },
			{ e_enter, "f", "5" },
			{ e_exit },
		{ e_exit },
		{ e_enter, "g", "7\n8" },
			{ e_enter, "h", "6" },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_enter, "i" },
	{ e_exit },
	{ e_finish }
};

static const char * test4 =
"Parent-1.0=A-value child child=\"B value\" child:C:\"value\"\n"
"  child:D:\"value\"\n"
"    grandchild\n"
"  child grandchild=A\n"
"    :multi-line\n"
"    :text-field\n"
"    grandchild=B foo=bar\n"
"      foo=bar\n"
"\n"
"Parent-1.0";
static bmlparser::event test4e[]={
	{ e_enter, "Parent-1.0", "A-value" },
		{ e_enter, "child" },
		{ e_exit },
		{ e_enter, "child", "B value" },
		{ e_exit },
		{ e_enter, "child", "C:\"value\"" },
		{ e_exit },
		{ e_enter, "child", "D:\"value\"" },
			{ e_enter, "grandchild" },
			{ e_exit },
		{ e_exit },
		{ e_enter, "child", "multi-line\ntext-field" },
			{ e_enter, "grandchild", "A" },
			{ e_exit },
			{ e_enter, "grandchild", "B" },
				{ e_enter, "foo", "bar" },
				{ e_exit },
				{ e_enter, "foo", "bar" },
				{ e_exit },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_enter, "Parent-1.0" },
	{ e_exit },
	{ e_finish }
};

//screwy whitespace on otherwise blank lines is probably allowed
//I can't justify allowing blank lines only,
//I can't justify allowing only one of same-as-above and same-as-below,
//and I can't justify allowing three different options but not all of them.
//therefore, only one option remains.
static const char * test5 =
"\n"
"a\n"
"  b\n"
"\n"
" \n"
"  \n"
"   \n"
"    \n"
"      \n"
"\t\n"
"    c\n";
static bmlparser::event test5e[]={
	{ e_enter, "a" },
		{ e_enter, "b" },
			{ e_enter, "c" },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_finish }
};

//comments
static const char * test6 =
"\n"
"#test\n"
" #test\n"
"a #test\n"
"b c #test\n"
"d:#test\n"
"e: #test\n"
"f\n"
" :g\n"
" :h\n"
"#x\n"
"i=j#k\n"
"\n"
"//test\n"
" //test\n"
"a //test\n"
"b c //test\n"
"d://test\n"
"e: //test\n"
"f\n"
" :g\n"
" :h\n"
"//x\n"
"i=j//k\n"
"\n"
;
static bmlparser::event test6e[]={
	{ e_enter, "a" }, { e_exit },
	{ e_enter, "b" }, { e_enter, "c" }, { e_exit }, { e_exit },
	{ e_enter, "d", "#test" }, { e_exit },
	{ e_enter, "e", "#test" }, { e_exit },
	{ e_enter, "f", "g\nh" }, { e_exit },
	{ e_enter, "i", "j#k" }, { e_exit },
	
	{ e_enter, "a" }, { e_exit },
	{ e_enter, "b" }, { e_enter, "c" }, { e_exit }, { e_exit },
	{ e_enter, "d", "//test" }, { e_exit },
	{ e_enter, "e", "//test" }, { e_exit },
	{ e_enter, "f", "g\nh" }, { e_exit },
	{ e_enter, "i", "j//k" }, { e_exit },
	{ e_finish }
};

static void testbml(const char * bml, bmlparser::event* expected)
{
	bmlparser parser(bml);
	bmlparser::event actual { e_error };
	while (true)
	{
		actual = parser.next();
		
//printf("e=%i [%s] [%s]\n", expected->action, (const char*)expected->name, (const char*)expected->value);
//printf("a=%i [%s] [%s]\n\n", actual.action,  (const char*)actual.name,    (const char*)actual.value);
		assert_eq(actual.action, expected->action);
		assert_eq(actual.name, expected->name);
		assert_eq(actual.value, expected->value);
		
		if (expected->action == e_finish || actual.action == e_finish) return;
		
		expected++;
	}
}

static void testbml_error(const char * bml)
{
	bmlparser parser(bml);
	int depth = 0;
	bool error = false;
	int events = 0;
	while (true)
	{
		bmlparser::event ev = parser.next();
//if (events==999)
//printf("a=%i [%s] [%s]\n\n", ev.action, ev.name.data().ptr(), ev.value.data().ptr());
		if (ev.action == e_error) error = true; // any error is fine
		if (ev.action == e_enter) depth++;
		if (ev.action == e_exit) depth--;
		if (ev.action == e_finish) break;
		assert(depth >= 0);
		
		events++;
		assert(events < 1000); // fail on infinite error loops
	}
	assert_eq(error, true);
	assert_eq(depth, 0);
}

test("BML parser", "string,array", "bml")
{
	testcall(testbml(test1, test1e));
	testcall(testbml(test2, test2e));
	testcall(testbml(test3, test3e));
	testcall(testbml(test4, test4e));
	testcall(testbml(test5, test5e));
	testcall(testbml(test6, test6e));
	
	testcall(testbml_error("*"));           // invalid node name
	testcall(testbml_error("a=\""));        // unclosed quote
	testcall(testbml_error("a=\"b\"c"));    // no space after closing quote
	testcall(testbml_error("a=\"b\"c\""));  // quote in quoted element
	testcall(testbml_error("a\n  b\n c"));  // derpy indentation
	testcall(testbml_error("a\n b\n\tc"));  // mixed tabs and spaces
	testcall(testbml_error("a=b\n :c"));    // two values
	testcall(testbml_error(" a"));          // can't indent root node
	testcall(testbml_error("a=b\"c"));      // no quote allowed in mode=eq
	testcall(testbml_error("a b=c\"d"));    // nor ieq
	testcall(testbml_error("a#a"));         // comment not allowed here
	testcall(testbml_error("a a#a"));       // nor here
	testcall(testbml_error("a//a"));        // nor this kind of comments
	testcall(testbml_error("a a//a"));      // nor here
	testcall(testbml_error("a=\"a\"#a\"")); // no quote allowed in eq, that # isn't a comment
	testcall(testbml_error("a=\"a\"#a"));   // not allowed like this either
	
	//derpy indentation with multilines
	testcall(testbml_error("a\n :b\n  :c"));     // no increasing indentation in a multiline
	testcall(testbml_error("a\n  :b\n :c"));     // nor decreasing
	testcall(testbml_error("a\n :b\n  c"));      // not even if switching from multiline to child
	testcall(testbml_error("a\n  :b\n c"));      // nor decreasing
	testcall(testbml_error("a\n b\n  :c\n :b")); // trickier case of the above
	testcall(testbml_error("a\n :b\n\t:c"));     // no tab-space mix
	testcall(testbml_error("a\n :b\n\tc"));      // no tab-space mix with multi->child
	
	testcall(testbml_error("a\n :b\n\n :c"));    // blank line in multiline, not allowed
	testcall(testbml_error("a\n :b\n \n :c"));   // not even if indented
	testcall(testbml_error("a\n\n :b"));         // not allowed at start either
	testcall(testbml_error("a\n \n :b"));        // still not even if indented
}
#endif
