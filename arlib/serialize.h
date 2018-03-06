#pragma once
#include "global.h"
#include "bml.h"
#include "json.h"
#include "stringconv.h"
#include "set.h"

#define SERIALIZE_CORE(member) s.item(STR(member), member);
#define SERIALIZE(...) template<typename T> void serialize(T& s) { PPFOREACH(SERIALIZE_CORE, __VA_ARGS__); }

//Interface:
//class serializer {
//public:
//	static const bool serializing;
//	
//	//Valid types:
//	//- Any integral type ('char' doesn't count as integral)
//	//- string (but not cstring)
//	//- array, set, map (if their contents are serializable)
//	//    map must use integer or string key, nothing funny
//	//    the serializer may ban array<array<T>> or set<set<T>>, though array<struct_with_array> is fine
//	//- Any object with a serialize() function (see below)
//	//The name can be any string.
//	template<typename T> void item(cstring name, T& item);
//	
//	//Similar to item(), but uses hex rather than decimal if output is human readable (otherwise, identical to item).
//	//Valid types:
//	//- Any unsigned integral type
//	//- array<byte>
//	//- arrayvieww<byte>
//	template<typename T> void hex(cstring name, T& item);
//	void hex(cstring name, arrayvieww<byte> item);
//	
//	//Makes serialized data look nicer. May be ignored.
//	void comment(cstring c);
//	
//	//Returns the next child name the structure expects to process. Valid only while unserializing.
//	cstring next() const;
//	//(BML unserializer only) Returns the value corresponding to next().
//	cstring nextval() const;
//};
//
//struct serializable {
//	int a;
//	
//public:
//	template<typename T> // T is guaranteed to offer the serializer interface.
//	void serialize(T& s)
//	{
//		//If unserializing, this function can be called multiple (or zero) times if the document is
//		// corrupt. Be careful about changing any state, other than calling the serializer functions.
//		//For most items, .item() and .hex() are enough. For containers, do whatever.
//		s.item("a", a);
//	}
//	//or (expands to the above)
//public:
//	SERIALIZE(a);
//};


class bmlserialize_impl {
	bmlwriter w;
	template<typename T> friend string bmlserialize(T& item);
	
	template<typename T> void add_node(cstring name, T& item)
	{
		w.enter(bmlwriter::escape(name), "");
		item.serialize(*this);
		w.exit();
	}
	
	template<typename T> void add_node(cstring name, array<T>& item)
	{
		for (auto& child : item)
		{
			add_node(name, child);
		}
	}
	
	template<typename T> void add_node(cstring name, set<T>& item)
	{
		for (auto const& child : item)
		{
			add_node(name, child);
		}
	}
	
	template<typename T> void add_node(cstring name, array<array<T>>& item) = delete;
	
#define LEAF(T) \
		void add_node(cstring name, T& item) { w.node(bmlwriter::escape(name), tostring(item)); } \
		void add_node(cstring name, const T& item) { w.node(bmlwriter::escape(name), tostring(item)); }
	ALLSTRINGABLE(LEAF);
#undef LEAF
	
public:
	
	static const bool serializing = true;
	
	void comment(cstring c)
	{
		w.comment(c);
	}
	
	template<typename T> void item(cstring name, T& item) { add_node(name, item); }
	
	template<typename T> void hex(cstring name, T& item)
	{
		w.node(bmlwriter::escape(name), tostringhex(item));
	}
	void hex(cstring name, arrayview<byte> item)
	{
		w.node(bmlwriter::escape(name), tostringhex(item));
	}
	
	cstring next() const { abort(); } // illegal
};

template<typename T> string bmlserialize(T& item)
{
	bmlserialize_impl s;
	item.serialize(s);
	return s.w.finish();
}



class bmlunserialize_impl {
	bmlparser p;
	int pdepth = 0;
	
	int thisdepth = 0;
	string thisnode;
	string thisval;
	bool matchagain;
	
	bmlparser::event event()
	{
		bmlparser::event ret = p.next();
		if (ret.action == bmlparser::enter) pdepth++;
		if (ret.action == bmlparser::exit) pdepth--;
		if (ret.action == bmlparser::finish) pdepth = -2;
		return ret;
	}
	
	void skipchildren()
	{
		while (pdepth > thisdepth) event();
	}
	
	template<typename T> friend T bmlunserialize(cstring bml);
	template<typename T> friend void bmlunserialize_to(cstring bml, T& to);
	
	template<typename T> void read_item(T& out)
	{
		while (pdepth >= thisdepth)
		{
			bmlparser::event ev = event();
			if (ev.action == bmlparser::enter)
			{
				thisdepth++;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
				do {
					matchagain = false;
					out.serialize(*this);
				} while (matchagain);
				thisdepth--;
				skipchildren();
			}
		}
	}
	
	template<typename T> void read_item(array<T>& out)
	{
		read_item(out.append());
	}
	
	template<typename T> void read_item(set<T>& out)
	{
		T tmp;
		read_item(tmp);
		out.add(tmp);
	}
	
	template<typename T> void read_item(array<array<T>>& item) = delete;
	
	void to_next()
	{
		matchagain = false;
		
		if (pdepth >= thisdepth)
		{
			thisdepth--;
			skipchildren();
			
			bmlparser::event ev = event();
			if (ev.action == bmlparser::enter)
			{
				matchagain = true;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
			}
			
			thisdepth++;
		}
	}
	
#define LEAF(T) void read_item(T& out) { fromstring(thisval, out); }
	ALLSTRINGABLE(LEAF);
#undef LEAF
	
public:
	bmlunserialize_impl(cstring bml) : p(bml) {}
	
	static const bool serializing = false;
	
	template<typename T> void item(cstring name, T& out)
	{
		while (thisnode == name) // this should be a loop, in case of documents like 'foo bar=1 bar=2 bar=3'
		{
			read_item(out);
			thisnode = "";
			to_next();
		}
	}
	
	bool enter(bool& first)
	{
		bmlparser::event ev(0);
		if (!first) goto l_matchagain;
		first = false;
		
		while (pdepth >= thisdepth)
		{
			ev = event();
			if (ev.action == bmlparser::enter)
			{
				thisdepth++;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
				do {
					matchagain = false;
					return true;
				l_matchagain: ;
				} while (matchagain);
				thisdepth--;
				skipchildren();
			}
		}
		thisnode = "";
		to_next();
		return false;
	}
	
	template<typename T> void hex(cstring name, T& out)
	{
		while (thisnode == name)
		{
			fromstringhex(thisval, out);
			thisnode = "";
			to_next();
		}
	}
	
	void hex(cstring name, arrayvieww<byte> out)
	{
		while (thisnode == name)
		{
			fromstringhex(thisval, out);
			thisnode = "";
			to_next();
		}
	}
	
	cstring next() const { return thisnode; }
	cstring nextval() const { return thisval; }
	
	void comment(cstring c) {}
};

template<typename T> T bmlunserialize(cstring bml)
{
	T out{};
	bmlunserialize_impl s(bml);
	s.read_item(out);
	return out;
}

template<typename T> void bmlunserialize_to(cstring bml, T& to)
{
	bmlunserialize_impl s(bml);
	s.read_item(to);
}

#define ser_enter(s) for (bool serialize_first = true; s.enter(serialize_first);)



//class jsonserialize_impl



class jsonunserialize_impl {
	jsonparser p;
	jsonparser::event ev;
	bool matchagain;
	
	jsonunserialize_impl(cstring json) : p(json) {}
	template<typename T> friend T jsonunserialize(cstring json);
	
	void finish_item()
	{
		if (ev.action == jsonparser::enter_map || ev.action == jsonparser::enter_list)
		{
			while (true)
			{
				ev = p.next();
				finish_item();
				if (ev.action == jsonparser::exit_map || ev.action == jsonparser::exit_list) break;
			}
		}
	}
	
#define LEAF(T) void read_item(T& out) { if (ev.action == jsonparser::num) out = ev.num; finish_item(); ev = p.next(); }
	ALLNUMS(LEAF);
#undef LEAF
	
	void read_item(string& out)
	{
		if (ev.action == jsonparser::str) out = ev.str;
		finish_item();
		ev = p.next();
	}
	
	template<typename T> void read_item(array<T>& out)
	{
		out.reset();
		if (ev.action == jsonparser::enter_list)
		{
			ev = p.next();
			while (ev.action != jsonparser::exit_list)
			{
				read_item(out.append());
			}
		}
		else finish_item();
		ev = p.next();
	}
	
	template<typename T> void read_item(set<T>& out)
	{
		out.reset();
		if (ev.action == jsonparser::enter_list)
		{
			ev = p.next();
			while (ev.action != jsonparser::exit_list)
			{
				T tmp;
				read_item(tmp);
				out.add(tmp);
			}
		}
		else finish_item();
		ev = p.next();
	}
	
	template<typename T> void read_item(T& out)
	{
		if (ev.action == jsonparser::enter_map)
		{
			ev = p.next();
			while (ev.action != jsonparser::exit_map)
			{
				matchagain = false;
				//ev = map_key
				out.serialize(*this);
				if (!matchagain)
				{
					ev = p.next();
					if (ev.action == jsonparser::exit_map) break; // can happen if document is broken
					//ev = enter_map or whatever
					finish_item();
					//ev = exit_map or whatever
					ev = p.next();
					//ev = map_key or exit_map
				}
//if(ev.action==jsonparser::finish)*(char*)0=0;
			}
		}
		else finish_item();
		ev = p.next();
	}
	
	
public:
	
	static const bool serializing = false;
	
	template<typename T> void item(cstring name, T& out)
	{
//puts(tostring(ev.action)+","+tostring(jsonparser::map_key)+" "+ev.str+","+name);
		//this should be a loop, in case of documents like '{ "foo": 1, "foo": 2, "foo": 3 }'
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			read_item(out);
			matchagain = true;
//puts("::"+tostring(ev.action)+": "+tostring(jsonparser::map_key)+","+tostring(jsonparser::exit_map));
		}
	}
	
	template<typename T> typename std::enable_if<std::is_integral<T>::value>::type hex(cstring name, T& out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			if (ev.action == jsonparser::num) out = ev.num;
			finish_item();
			ev = p.next();
			matchagain = true;
		}
	}
	
	void hex(cstring name, arrayvieww<byte> out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			if (ev.action == jsonparser::str)
			{
				fromstringhex(ev.str, out);
			}
			finish_item();
			ev = p.next();
			matchagain = true;
		}
	}
	
	void hex(cstring name, array<byte>& out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			if (ev.action == jsonparser::str)
			{
				fromstringhex(ev.str, out);
			}
			finish_item();
			ev = p.next();
			matchagain = true;
		}
	}
	
	cstring next() const
	{
		if (ev.action == jsonparser::map_key) return ev.str;
		else return "";
	}
	
	void comment(cstring c) {}
};

template<typename T> T jsonunserialize(cstring json)
{
	T out{};
	jsonunserialize_impl s(json);
	s.ev = s.p.next();
	s.read_item(out);
	return out;
}
