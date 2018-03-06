#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "stringconv.h"
#include "set.h"

//This is a streaming parser. It returns a sequence of event objects.
//For example, the document
/*
{ "foo": [ 1, 2, 3 ] }
*/
//would yield { enter_map } { map_key, "foo" } { enter_list } { num, 1 } { num, 2 } { num, 3 } { exit_list } { exit_map }
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more, or misplaced, nodes.
//enter/exit types are always paired, even in the presense of errors. However, they may be anywhere;
// don't place any expectations on event order inside a map.
//After the document ends, { finish } will be returned forever until the object is deleted.
class jsonparser : nocopy {
public:
	enum {
		unset      = 0,
		jtrue      = 1,
		jfalse     = 2,
		jnull      = 3,
		str        = 4,
		num        = 5,
		enter_list = 6,
		exit_list  = 7,
		enter_map  = 8,
		map_key    = 9,
		exit_map   = 10,
		error      = 11,
		finish     = 12,
	};
	struct event {
		int action;
		string str; // or error message [TODO: actual error messages]
		double num;
		
		event() : action(unset) {}
		event(int action) : action(action) {}
		event(int action, cstring str) : action(action), str(str) {}
		event(int action, double num) : action(action), num(num) {}
	};
	
	//You can't stream data into this object.
	jsonparser(cstring json) : m_data(json) {}
	event next();
	bool errored() { return m_errored; }
	
private:
	cstring m_data;
	size_t m_pos = 0;
	bool m_want_key = false; // true if inside {}
	bool m_need_value = true; // true after a comma or at the start of the object
	
	bool m_unexpected_end = false; // used to avoid sending the same error again after the document "{" whines
	bool m_errored = false;
	event do_error() { m_errored=true; return { error }; }
	
	array<bool> m_nesting; // an entry is false for list, true for map; after [[{, this is false,false,true
	
	uint8_t nextch();
	bool skipcomma(size_t depth = 1);
	
	string getstr();
};


//This is also streaming.
//Calling exit() without a matching enter(), or finish() without closing every enter(), is undefined behavior.
class jsonwriter {
	string m_data;
	bool m_comma = false;
	
	void comma() { if (m_comma) m_data += ','; m_comma = true; }
	
public:
	static string strwrap(cstring s)
	{
		string out = "\"";
		for (size_t i=0;i<s.length();i++)
		{
			char c = s[i];
			if(0);
			else if (c=='\n') out+="\\n";
			else if (c=='\t') out += "\\t";
			else if (c=='"') out += "\\\"";
			else if (c=='\\') out += "\\\\";
			else out += c;
		}
		return out+"\"";
	}
	
	void null() { comma(); m_data += "null"; }
	void boolean(bool b) { comma(); m_data += b ? "true" : "false"; }
	void str(cstring s) { comma(); m_data += strwrap(s); }
	void num(int n)    { comma(); m_data += tostring(n); }
	void num(size_t n) { comma(); m_data += tostring(n); }
	void num(double n) { comma(); m_data += tostring(n); }
	void list_enter() { comma(); m_data += "["; m_comma = false; }
	void list_exit() { m_data += "]"; m_comma = true; }
	void map_enter() { comma(); m_data += "{"; m_comma = false; }
	void map_key(cstring s) { comma(); m_data += strwrap(s); m_data += ":"; m_comma = false; }
	void map_exit() { m_data += "}"; m_comma = true; }
	
	string finish() { return m_data; }
};



class JSON {
	jsonparser::event ev;
	array<JSON> chld_list;
	map<string,JSON> chld_map;
	
	void construct(jsonparser& p, bool* ok, size_t maxdepth);
	void serialize(jsonwriter& w);
	
public:
	JSON() : ev(jsonparser::jnull) {}
	JSON(cstring s) { parse(s); }
	JSON(const JSON& other) = delete;
	
	bool parse(cstring s);
	string serialize();
	
	double num() { ev.action = jsonparser::num; return ev.num; }
	string& str() { ev.action = jsonparser::str; return ev.str; }
	array<JSON>& list() { ev.action = jsonparser::enter_list; return chld_list; }
	map<string,JSON>& assoc() { ev.action = jsonparser::enter_map; return chld_map; } // 'map' is taken
	
	bool boolean() const
	{
		switch (ev.action)
		{
		case jsonparser::unset: return false;
		case jsonparser::jtrue: return true;
		case jsonparser::jfalse: return false;
		case jsonparser::jnull: return false;
		case jsonparser::str: return ev.str;
		case jsonparser::num: return ev.num;
		case jsonparser::enter_list: return chld_list.size();
		case jsonparser::enter_map: return chld_map.size();
		case jsonparser::error: return false;
		default: abort(); // unreachable
		}
	}
	
	operator bool() const { return boolean(); }
	operator int() const { return ev.num; }
	operator size_t() const { return ev.num; }
	operator double() const { return ev.num; }
	//operator string&() const { return ev.str; }
	operator cstring() const { return ev.str; }
	//operator const char *() { return str(); }
	
	operator bool() { return boolean(); }
	operator int() { return num(); }
	operator size_t() { return num(); }
	operator double() { return num(); }
	operator string&() { return str(); }
	operator cstring() { return str(); }
	
	bool operator==(int right) { return num()==right; }
	bool operator==(size_t right) { return num()==right; }
	bool operator==(double right) { return num()==right; }
	bool operator==(const char * right) { return str()==right; }
	bool operator==(cstring right) { return str()==right; }
	
	bool operator!=(int right) { return num()!=right; }
	bool operator!=(size_t right) { return num()!=right; }
	bool operator!=(double right) { return num()!=right; }
	bool operator!=(const char * right) { return str()!=right; }
	bool operator!=(cstring right) { return str()!=right; }
	
	JSON& operator=(nullptr_t) { ev.action = jsonparser::jnull; return *this; }
	JSON& operator=(bool b) { ev.action = b ? jsonparser::jtrue : jsonparser::jfalse; return *this; }
	JSON& operator=(size_t n) { return operator=((double)n); }
	JSON& operator=(int n) { return operator=((double)n); }
	JSON& operator=(double n) { ev.action = jsonparser::num; ev.num = n; return *this; }
	JSON& operator=(cstring s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	JSON& operator=(string s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	JSON& operator=(const char * s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	
	JSON& operator[](int n) { return list()[n]; }
	JSON& operator[](size_t n) { return list()[n]; }
	JSON& operator[](const char * s) { return assoc().get_create(s); }
	JSON& operator[](cstring s) { return assoc().get_create(s); }
	
	JSON& operator[](const JSON& right)
	{
		if (right.ev.action == jsonparser::str) return assoc().get_create(right.ev.str);
		else return list()[right.ev.num];
	}
};
