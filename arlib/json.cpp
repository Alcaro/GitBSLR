#include "json.h"
#include "stringconv.h"

static bool l_isspace(char ch)
{
	return (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
}

uint8_t jsonparser::nextch()
{
again: ;
	uint8_t ret = m_data[m_pos++];
	if (ret >= 33) return ret;
	if (l_isspace(ret)) goto again;
	if (m_pos > m_data.length())
	{
		m_pos--;
		return '\0';
	}
	return '\1'; // just some arbitrary invalid character
}

//returns false if the input is invalid
bool jsonparser::skipcomma(size_t depth)
{
	uint8_t ch = nextch();
	if (ch == ',' || ch == '\0')
	{
		if ((m_nesting.size() >= depth) == (ch == '\0')) return false;
		if (m_nesting.get_or(m_nesting.size()-depth, false) == true)
		{
			m_want_key = true;
		}
		if (ch == ',')
		{
			m_need_value = true;
		}
		return true;
	}
	if (ch == ']' || ch == '}')
	{
		m_pos--;
		return true;
	}
	return false;
}

jsonparser::event jsonparser::next()
{
	uint8_t ch = nextch();
	if (m_want_key)
	{
		m_want_key = false;
		if (ch == '"') goto parse_key;
		else if (ch == '}') goto close_brace;
		else return do_error();
	}
	
	if (ch == '\0')
	{
		if (m_need_value)
		{
			m_need_value = false;
			return do_error();
		}
		if (m_nesting)
		{
			if (!m_unexpected_end)
			{
				m_unexpected_end = true;
				return do_error();
			}
			bool map = (m_nesting[m_nesting.size()-1] == true);
			m_nesting.resize(m_nesting.size()-1);
			return { map ? exit_map : exit_list };
		}
		return { finish };
	}
	if (m_need_value && (ch == ']' || ch == '}'))
	{
		m_need_value = false;
		m_pos--;
		return do_error();
	}
	m_need_value = false;
	
	if (ch == '"')
	{
		bool is_key;
		is_key = false;
		if (false)
		{
		parse_key:
			is_key = true;
		}
		string val;
		while (true)
		{
			uint8_t ch = m_data[m_pos++];
			if (ch < 32)
			{
				m_pos--;
				return do_error();
			}
			if (ch == '\\')
			{
				uint8_t esc = m_data[m_pos++];
				switch (esc)
				{
				case '"': val += '"'; break;
				case '\\': val += '\\'; break;
				case '/': val += '/'; break;
				case 'b': val += '\b'; break;
				case 'f': val += '\f'; break;
				case 'n': val += '\n'; break;
				case 'r': val += '\r'; break;
				case 't': val += '\t'; break;
				case 'u':
				{
					if (m_pos+4 > m_data.length()) return do_error();
					string unichar;
					unichar += m_data[m_pos++];
					unichar += m_data[m_pos++];
					unichar += m_data[m_pos++];
					unichar += m_data[m_pos++];
					uint16_t codepoint;
					if (!fromstringhex(unichar, codepoint)) return do_error();
					val += string::codepoint(codepoint);
					break;
				}
				default:
					m_pos--;
					return do_error();
				}
				continue;
			}
			if (ch == '"') break;
			val += ch;
		}
		if (is_key)
		{
			if (nextch() != ':') return do_error();
			return { map_key, val };
		}
		else
		{
			if (!skipcomma()) return do_error();
			return { str, val };
		}
	}
	if (ch == '[')
	{
		m_nesting.append(false);
		return { enter_list };
	}
	if (ch == ']')
	{
		if (!m_nesting || m_nesting[m_nesting.size()-1] != false) return do_error();
		if (!skipcomma(2)) return do_error();
		m_nesting.resize(m_nesting.size()-1);
		return { exit_list };
	}
	if (ch == '{')
	{
		m_nesting.append(true);
		m_want_key = true;
		return { enter_map };
	}
	if (ch == '}')
	{
	close_brace:
		m_want_key = false;
		if (!m_nesting || m_nesting[m_nesting.size()-1] != true) return do_error();
		if (!skipcomma(2)) return do_error();
		m_nesting.resize(m_nesting.size()-1);
		return { exit_map };
	}
	if (ch == '-' || isdigit(ch))
	{
		m_pos--;
		size_t start = m_pos;
		if (m_data[m_pos] == '-') m_pos++;
		if (m_data[m_pos] == '0') m_pos++;
		else
		{
			if (!isdigit(m_data[m_pos])) return do_error();
			while (isdigit(m_data[m_pos])) m_pos++;
		}
		if (m_data[m_pos] == '.')
		{
			m_pos++;
			if (!isdigit(m_data[m_pos])) return do_error();
			while (isdigit(m_data[m_pos])) m_pos++;
		}
		if (m_data[m_pos] == 'e' || m_data[m_pos] == 'E')
		{
			m_pos++;
			if (m_data[m_pos] == '+' || m_data[m_pos] == '-') m_pos++;
			if (!isdigit(m_data[m_pos])) return do_error();
			while (isdigit(m_data[m_pos])) m_pos++;
		}
		
		double d;
		if (!fromstring(m_data.substr(start, m_pos), d)) return do_error();
		if (!skipcomma()) return do_error();
		return { num, d };
	}
	if (ch == 't' && m_data[m_pos++]=='r' && m_data[m_pos++]=='u' && m_data[m_pos++]=='e')
	{
		if (!skipcomma()) return do_error();
		return { jtrue };
	}
	if (ch == 'f' && m_data[m_pos++]=='a' && m_data[m_pos++]=='l' && m_data[m_pos++]=='s' && m_data[m_pos++]=='e')
	{
		if (!skipcomma()) return do_error();
		return { jfalse };
	}
	if (ch == 'n' && m_data[m_pos++]=='u' && m_data[m_pos++]=='l' && m_data[m_pos++]=='l')
	{
		if (!skipcomma()) return do_error();
		return { jnull };
	}
	
	return do_error();
}

void JSON::construct(jsonparser& p, bool* ok, size_t maxdepth)
{
	if (maxdepth == 0)
	{
		*ok = false;
		if (ev.action == jsonparser::enter_list || ev.action == jsonparser::enter_map)
		{
			size_t xdepth = 1;
			while (xdepth)
			{
				jsonparser::event next = p.next();
				if (next.action == jsonparser::enter_list) xdepth++;
				if (next.action == jsonparser::enter_map) xdepth++;
				if (next.action == jsonparser::exit_list) xdepth--;
				if (next.action == jsonparser::exit_map) xdepth--;
			}
		}
		return;
	}
	
	if (ev.action == jsonparser::enter_list)
	{
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.action == jsonparser::exit_list) break;
			if (next.action == jsonparser::error) *ok = false;
			JSON& child = chld_list.append();
			child.ev = next;
			child.construct(p, ok, maxdepth-1);
		}
	}
	if (ev.action == jsonparser::enter_map)
	{
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.action == jsonparser::exit_map) break;
			if (next.action == jsonparser::map_key)
			{
				JSON& child = chld_map.insert(next.str);
				child.ev = p.next();
				child.construct(p, ok, maxdepth-1);
			}
			if (next.action == jsonparser::error) *ok = false;
		}
	}
	if (ev.action == jsonparser::error) *ok = false;
}

void JSON::serialize(jsonwriter& w)
{
	switch (ev.action)
	{
	case jsonparser::unset:
	case jsonparser::error:
	case jsonparser::jnull:
		w.null();
		break;
	case jsonparser::jtrue:
		w.boolean(true);
		break;
	case jsonparser::jfalse:
		w.boolean(false);
		break;
	case jsonparser::str:
		w.str(ev.str);
		break;
	case jsonparser::num:
		if (ev.num == (int)ev.num) w.num((int)ev.num);
		else w.num(ev.num);
		break;
	case jsonparser::enter_list:
		w.list_enter();
		for (JSON& j : chld_list) j.serialize(w);
		w.list_exit();
		break;
	case jsonparser::enter_map:
		w.map_enter();
		for (auto& e : chld_map)
		{
			w.map_key(e.key);
			e.value.serialize(w);
		}
		w.map_exit();
		break;
	default: abort(); // unreachable
	}
}

bool JSON::parse(cstring s)
{
	chld_list.reset();
	chld_map.reset();
	
	jsonparser p(s);
	ev = p.next();
	bool ok = true;
	construct(p, &ok, 1000);
	jsonparser::event lastev = p.next();
	if (!ok || lastev.action != jsonparser::finish)
	{
		ev.action = jsonparser::error;
		return false;
	}
	return true;
}

string JSON::serialize()
{
	jsonwriter w;
	serialize(w);
	return w.finish();
}


#include "test.h"
#ifdef ARLIB_TEST
#define e_jfalse jsonparser::jfalse
#define e_jtrue jsonparser::jtrue
#define e_jnull jsonparser::jnull
#define e_str jsonparser::str
#define e_num jsonparser::num
#define e_enter_list jsonparser::enter_list
#define e_exit_list jsonparser::exit_list
#define e_enter_map jsonparser::enter_map
#define e_map_key jsonparser::map_key
#define e_exit_map jsonparser::exit_map
#define e_error jsonparser::error
#define e_finish jsonparser::finish

static const char * test1 =
"\"x\"\n"
;

static jsonparser::event test1e[]={
	{ e_str, "x" },
	{ e_finish }
};

static const char * test2 =
"[ 1, 2.5e+1, 3 ]"
;

static jsonparser::event test2e[]={
	{ e_enter_list },
		{ e_num, 1 },
		{ e_num, 25 },
		{ e_num, 3 },
	{ e_exit_list },
	{ e_finish }
};

static const char * test3 =
"{ \"foo\": [ true, false, null ] }\n"
;

static jsonparser::event test3e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_enter_list },
			{ e_jtrue },
			{ e_jfalse },
			{ e_jnull },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test4 =
"{ \"a\": [ { \"b\": [ 1, 2 ], \"c\": [ 3, 4 ] }, { \"d\": [ 5, 6 ], \"e\": [ 7, 8 ] } ],\n"
"  \"f\": [ { \"g\": [ 9, 0 ], \"h\": [ 1, 2 ] }, { \"i\": [ 3, \"\xC3\xB8\" ], \"j\": [ {}, \"x\\nx\" ] } ] }"
;

static jsonparser::event test4e[]={
	{ e_enter_map },
		{ e_map_key, "a" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "b" },
				{ e_enter_list }, { e_num, 1 }, { e_num, 2 }, { e_exit_list },
				{ e_map_key, "c" },
				{ e_enter_list }, { e_num, 3 }, { e_num, 4 }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "d" },
				{ e_enter_list }, { e_num, 5 }, { e_num, 6 }, { e_exit_list },
				{ e_map_key, "e" },
				{ e_enter_list }, { e_num, 7 }, { e_num, 8 }, { e_exit_list },
			{ e_exit_map },
		{ e_exit_list },
		{ e_map_key, "f" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "g" },
				{ e_enter_list }, { e_num, 9 }, { e_num, 0 }, { e_exit_list },
				{ e_map_key, "h" },
				{ e_enter_list }, { e_num, 1 }, { e_num, 2 }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "i" },
				{ e_enter_list }, { e_num, 3 }, { e_str, "\xC3\xB8" }, { e_exit_list },
				{ e_map_key, "j" },
				{ e_enter_list }, { e_enter_map }, { e_exit_map }, { e_str, "x\nx" }, { e_exit_list },
			{ e_exit_map },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test5 =
"{ \"foo\": \"\x80\\u0080\" }\n"
;

static jsonparser::event test5e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_str, "\x80\xC2\x80" },
	{ e_exit_map },
	{ e_finish }
};

static void testjson(cstring json, jsonparser::event* expected)
{
	jsonparser parser(json);
	while (true)
	{
		jsonparser::event actual = parser.next();
		
//printf("e=%i [%s] [%lf]\n", expected->action, (const char*)expected->str.c_str(), (expected->action==e_num ? expected->num : -1));
//printf("a=%i [%s] [%lf]\n\n", actual.action,  (const char*)actual.str.c_str(),    (actual.action==e_num ? actual.num : -1));
		if (expected)
		{
			assert_eq(actual.action, expected->action);
			assert_eq(actual.str, expected->str);
			if (expected->action == e_num) assert_eq(actual.num, expected->num);
			
			if (expected->action == e_finish) return;
		}
		if (actual.action == e_finish) return;
		
		expected++;
	}
}

static void testjson_error(cstring json, bool actually_error = true)
{
	jsonparser parser(json);
	int depth = 0;
	bool error = false;
	int events = 0;
	while (true)
	{
		jsonparser::event ev = parser.next();
//if (events==999)
//printf("a=%i [%s] [%f]\n", ev.action, ev.str.bytes().ptr(), ev.num);
		if (ev.action == e_error) error = true; // any error is fine
		if (ev.action == e_enter_list || ev.action == e_enter_map) depth++;
		if (ev.action == e_exit_list  || ev.action == e_exit_map)  depth--;
		if (ev.action == e_finish) break;
		assert(depth >= 0);
		
		events++;
		assert_lt(events, 1000); // fail on infinite error loops
	}
	assert_eq(error, actually_error);
	assert_eq(depth, 0);
}

test("JSON parser", "string", "json")
{
	testcall(testjson(test1, test1e));
	testcall(testjson(test2, test2e));
	testcall(testjson(test3, test3e));
	testcall(testjson(test4, test4e));
	testcall(testjson(test5, test5e));
	
	testcall(testjson_error(""));
	testcall(testjson_error("{"));
	testcall(testjson_error("{\"a\""));
	testcall(testjson_error("{\"a\":"));
	testcall(testjson_error("{\"a\":1"));
	testcall(testjson_error("{\"a\":1,"));
	testcall(testjson_error("["));
	testcall(testjson_error("[1"));
	testcall(testjson_error("[1,"));
	testcall(testjson_error("\""));
	testcall(testjson_error("01"));
	testcall(testjson_error("1."));
	testcall(testjson_error("1e"));
	testcall(testjson_error("1e+"));
	testcall(testjson_error("1e-"));
	testcall(testjson_error("z"));
	testcall(testjson_error("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }"));
	testcall(testjson_error("\""));
	testcall(testjson_error("\"\\"));
	testcall(testjson_error("\"\\u"));
	testcall(testjson_error("\"\\u1"));
	testcall(testjson_error("\"\\u12"));
	testcall(testjson_error("\"\\u123"));
	testcall(testjson_error("\"\\u1234"));
	
	//try to make it read out of bounds
	//input length 31
	testcall(testjson_error("\"force allocating the string \\u"));
	testcall(testjson_error("\"force allocating the string\\u1"));
	testcall(testjson_error("\"force allocating the strin\\u12"));
	testcall(testjson_error("\"force allocating the stri\\u123"));
	testcall(testjson_error("\"force allocating the str\\u1234"));
	//input length 32
	testcall(testjson_error("\"force allocating the string  \\u"));
	testcall(testjson_error("\"force allocating the string \\u1"));
	testcall(testjson_error("\"force allocating the string\\u12"));
	testcall(testjson_error("\"force allocating the strin\\u123"));
	testcall(testjson_error("\"force allocating the stri\\u1234"));
	//input length 15
	testcall(testjson_error("\"inline data \\u"));
	testcall(testjson_error("\"inline data\\u1"));
	testcall(testjson_error("\"inline dat\\u12"));
	testcall(testjson_error("\"inline da\\u123"));
	testcall(testjson_error("\"inline d\\u1234"));
	
	//found by https://github.com/nst/JSONTestSuite/ - thanks!
	testcall(testjson_error("[]\1"));
	testcall(testjson_error("[],"));
	testcall(testjson_error("123,"));
	testcall(testjson_error("[\"\t\"]"));
	testcall(testjson_error("[1,]"));
	testcall(testjson_error("{\"a\":0,}"));
	testcall(testjson_error("[1,,2]"));
	testcall(testjson_error("[-.123]"));
	testcall(testjson_error(arrayview<byte>((uint8_t*)"123\0", 4)));
	testcall(testjson_error(arrayview<byte>((uint8_t*)"[]\0", 3)));
}



test("JSON container", "string,array,set", "json")
{
	{
		JSON json("7");
		assert_eq((int)json, 7);
	}
	
	{
		JSON json("\"42\"");
		assert_eq(json.str(), "42");
	}
	
	{
		JSON json("[1,2,3]");
		assert_eq((int)json[0], 1);
		assert_eq((int)json[1], 2);
		assert_eq((int)json[2], 3);
	}
	
	{
		JSON json("{\"a\":null,\"b\":true,\"c\":false}");
		assert_eq((bool)json["a"], false);
		assert_eq((bool)json["b"], true);
		assert_eq((bool)json["c"], false);
	}
	
	{
		JSON("["); // these pass if they do not yield infinite loops
		JSON("[{}");
		JSON("[[]");
		JSON("{");
		JSON("{\"x\"");
		JSON("{\"x\":");
	}
}
#endif
