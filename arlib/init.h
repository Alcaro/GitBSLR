#pragma once
#include "string.h"
#include "stringconv.h"

class argparse {
	class arg_base {
		friend class argparse;
		
	protected:
		arg_base(bool accept_no_value, bool accept_value) : accept_no_value(accept_no_value), accept_value(accept_value) {}
		
		string name;
		char sname;
		
		bool accept_no_value;
		bool accept_value;
		
		bool must_use = false;
		bool can_use = true;
		bool can_use_multi = false;
		
		virtual bool parse(cstring arg) = 0;
	public:
		virtual ~arg_base() {}
		
		friend void arlib_init_gui(argparse& args, char** argv);
	};
	
	template<typename T>
	class arg_t : public arg_base {
	protected:
		arg_t(bool accept_no_value, bool accept_value) : arg_base(accept_no_value, accept_value) {}
		
	public:
		T& required()
		{
			this->must_use = true;
			return *(T*)this;
		}
	};
	
	string m_appname;
	refarray<arg_base> m_args;
	function<void(cstring error)> m_onerror;
	bool m_accept_cli = false;
	bool m_has_gui = false;
	
	//sname can be '\0' or absent if you only want long names
	//if the long name is empty, all non-option arguments are sent here; if nothing has a long name, passing non-options is an error
	
private:
	class arg_str : public arg_t<arg_str> {
		friend class argparse;
		
		arg_str(string* target) : arg_t(false, true), target(target) {}
		~arg_str() {}
		string* target;
		bool parse(cstring arg) { return fromstring(arg, *target); }
	public:
		//none
	};
public:
	arg_str& add(char sname, cstring name, string* target);
	arg_str& add(cstring name, string* target) { return add('\0', name, target); }
	
private:
	class arg_strmany : public arg_t<arg_strmany> {
		friend class argparse;
		
		arg_strmany(array<string>* target) : arg_t(false, true), target(target) { this->can_use_multi = true; }
		array<string>* target;
		bool parse(cstring arg) { target->append(arg); return true; }
	public:
		//none
	};
public:
	arg_strmany& add(char sname, cstring name, array<string>* target);
	arg_strmany& add(cstring name, array<string>* target) { return add('\0', name, target); }
	
private:
	class arg_int : public arg_t<arg_int> {
		friend class argparse;
		
		arg_int(int* target) : arg_t(false, true), target(target) {}
		int* target;
		bool parse(cstring arg) { return fromstring(arg, *target); }
	public:
		//none
	};
public:
	arg_int& add(char sname, cstring name, int* target);
	arg_int& add(cstring name, int* target) { return add('\0', name, target); }
	
private:
	class arg_bool : public arg_t<arg_bool> {
		friend class argparse;
		
		arg_bool(bool* target) : arg_t(true, false), target(target) {}
		bool* target;
		bool parse(cstring arg) { *target = true; return true; }
	public:
		//none
	};
public:
	arg_bool& add(char sname, cstring name, bool* target);
	arg_bool& add(cstring name, bool* target) { return add('\0', name, target); }
	
	void add_cli() { m_accept_cli = true; }
	bool has_gui() { return m_has_gui; }
	
private:
	string get_usage();
	void usage();
	void error(cstring why);
	void single_arg(arg_base& arg, cstring value, bool must_use_value, bool* used_value);
	void single_arg(cstring name, cstring value, bool must_use_value, bool* used_value);
	void single_arg(char sname, cstring value, bool must_use_value, bool* used_value);
	
	void set_appname(cstring name)
	{
		m_appname = name;
	}
	
public:
	//The handler should not return; if it does, the default handler (print error to stderr and terminate) is called.
	//If you want to do something else, throw.
	void onerror(function<void(cstring error)> handler)
	{
		m_onerror = handler;
	}
	
private:
	//if the next argument is an option (starts with - and is not just -), don't use it
	const char * next_if_appropriate(const char * arg);
	
	void parse_pre(const char * const * argv);
	void parse_post(bool has_gui);
	
public:
	void parse(const char * const * argv);
	
	friend void arlib_init_gui(argparse& args, char** argv);
};

//This must be the first Arlib function called in main(), other than argparse setup. Be careful with static initializers too.
//Give it argv, and a descriptor of the arguments supported by this program.
//Automatically adds support for a few arguments, like --help, and --display on Linux if GUI is enabled.
//NULL means no arguments supported (if any present, throws error; still supports --help and --display).

void arlib_init(argparse& args, char** argv);
void arlib_init(nullptr_t, char** argv);


//Called by arlib_init(). Don't use them yourself.
void arlib_init_file();
void arlib_init_gui(argparse& args, char** argv);
