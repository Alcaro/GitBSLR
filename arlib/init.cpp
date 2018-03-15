#include "init.h"
#include "runloop.h"
#include "stringconv.h"

argparse::arg_str& argparse::add(char sname, cstring name, string* target)
{
	arg_str* arg = new arg_str(target);
	arg->name = name;
	arg->sname = sname;
	
	m_args.append_take(*arg);
	return *arg;
}

argparse::arg_strmany& argparse::add(char sname, cstring name, array<string>* target)
{
	arg_strmany* arg = new arg_strmany(target);
	arg->name = name;
	arg->sname = sname;
	
	m_args.append_take(*arg);
	return *arg;
}

argparse::arg_int& argparse::add(char sname, cstring name, int* target)
{
	arg_int* arg = new arg_int(target);
	arg->name = name;
	arg->sname = sname;
	
	m_args.append_take(*arg);
	return *arg;
}

argparse::arg_bool& argparse::add(char sname, cstring name, bool* target)
{
	arg_bool* arg = new arg_bool(target);
	arg->name = name;
	arg->sname = sname;
	
	m_args.append_take(*arg);
	return *arg;
}

string argparse::get_usage()
{
	return "TODO";
}
void argparse::usage()
{
	puts(get_usage());
	exit(0);
}
void argparse::error(cstring why)
{
	m_onerror(why);
	
	fputs(m_appname+": "+why+"\n", stderr);
	exit(1);
}
void argparse::single_arg(arg_base& arg, cstring value, bool must_use_value, bool* used_value)
{
	if (!arg.can_use) error("can't use --"+arg.name+" more than once");
	if (!arg.can_use_multi) arg.can_use = false;
	arg.must_use = false;
	
	if (used_value) *used_value = false;
	
	if (!value && !arg.accept_no_value) error("missing argument for --"+arg.name);
	if (must_use_value && !arg.accept_value) error("--"+arg.name+" doesn't allow an argument");
	
	if (value && arg.accept_value)
	{
		if (!arg.parse(value)) error("invalid value for --"+arg.name);
		if (used_value) *used_value = true;
	}
	else
	{
		arg.parse("");
	}
}
void argparse::single_arg(cstring name, cstring value, bool must_use_value, bool* used_value)
{
	for (arg_base& arg : m_args)
	{
		if (arg.name == name)
		{
			single_arg(arg, value, must_use_value, used_value);
			return;
		}
	}
	if (name != "") error("unknown argument: --"+name);
	else error("non-arguments not supported");
}
void argparse::single_arg(char sname, cstring value, bool must_use_value, bool* used_value)
{
	for (arg_base& arg : m_args)
	{
		if (arg.sname == sname)
		{
			single_arg(arg, value, must_use_value, used_value);
			return;
		}
	}
	error((string)"unknown argument: -"+sname);
}

const char * argparse::next_if_appropriate(const char * arg)
{
	if (!arg) return NULL;
	if (arg[0] != '-' || arg[1] == '\0') return arg;
	else return NULL;
}
void argparse::parse_pre(const char * const * argv)
{
	if (m_appname)
	{
		error("internal error: argparse::parse called twice");
	}
	m_appname = argv[0];
}
void argparse::parse(const char * const * argv)
{
	parse_pre(argv);
	
	argv++;
	while (*argv)
	{
		const char * arg = *argv;
		argv++;
		if (arg[0] == '-')
		{
			if (arg[1] == '-')
			{
				if (arg[2] == '\0')
				{
					// just --
					while (*argv)
					{
						single_arg("", *argv, true, NULL);
						argv++;
					}
				}
				else
				{
					// -- followed by something
					
					// +3 to skip the --, and the first actual character - otherwise '--=derp' would act like 'derp'
					const char * eq = strchr(arg+3, '=');
					if (eq)
					{
						single_arg(arrayview<char>(arg+2, eq-(arg+2)), eq+1, true, NULL);
					}
					else
					{
						bool used;
						single_arg(arg+2, next_if_appropriate(*argv), false, &used);
						if (used) argv++;
					}
				}
			}
			else if (arg[1] == '\0')
			{
				// a lone -
				single_arg("", "-", true, NULL);
			}
			else
			{
				// - followed by something
				
				arg++;
				while (*arg)
				{
					if (arg[1])
					{
						bool used;
						single_arg(*arg, arg+1, false, &used);
						if (used) break;
					}
					else
					{
						bool used;
						single_arg(*arg, next_if_appropriate(*argv), false, &used);
						if (used) argv++;
					}
					arg++;
				}
			}
		}
		else
		{
			// no leading -
			single_arg("", arg, true, NULL);
		}
	}
	m_has_gui = false;
	
	parse_post();
}

void argparse::parse_post()
{
	for (arg_base& arg : m_args)
	{
		if (arg.must_use) error("missing required argument --"+arg.name);
	}
}


#include "test.h"

template<typename... Args>
static void test_one_pack(int bools, int strs, int ints, cstring extras, bool error, const char * const argvp[])
{
	bool b[5] = { };
	string s[5] = { };
	int i[5] = { };
	array<string> extra;
	
	argparse args;
	args.add('a', "bool",  &b[0]);
	args.add('b', "bool2", &b[1]);
	args.add('c', "bool3", &b[2]);
	args.add('d', "bool4", &b[3]);
	args.add('e', "bool5", &b[4]);
	args.add('A', "str",  &s[0]);
	args.add('B', "str2", &s[1]);
	args.add('C', "str3", &s[2]);
	args.add('D', "str4", &s[3]);
	args.add('E', "str5", &s[4]);
	args.add("int",  &i[0]);
	args.add("int2", &i[1]);
	args.add("int3", &i[2]);
	args.add("int4", &i[3]);
	args.add("int5", &i[4]);
	if (extras) args.add("", &extra);
	
	if (error) args.onerror([](cstring){ throw 42; });
	else args.onerror([](cstring error){ puts(error.c_str()); assert_unreachable(); });
	
	try {
		args.parse(argvp);
	}
	catch(int) {
		if (error) return;
		else throw;
	}
	if (error) assert_unreachable();
	
	for (int j=0;j<bools;j++) assert(b[j]);
	for (int j=0;j<strs;j++)
	{
		if (s[j] != "-value") assert_eq(s[j], "value"); // accept both
	}
	for (int j=0;j<ints;j++) assert_eq(i[j], 42);
	
	assert_eq(extra.join("/"), extras);
}

template<typename... Args>
static void test_one(int bools, int strs, int ints, cstring extras, Args... argv)
{
	const char * const argvp[] = { "x", argv..., NULL };
	test_one_pack(bools, strs, ints, extras, false, argvp);
}

template<typename... Args>
static void test_error(Args... argv)
{
	const char * const argvp[] = { "x", argv..., NULL };
	test_one_pack(0, 0, 0, "", true, argvp);
}

#ifdef ARLIB_TEST
test("argument parser", "string,array", "argparse")
{
	test_one(1, 0, 0, "", "--bool");
	test_one(0, 1, 0, "", "--str=value");
	test_one(1, 0, 0, "arg/--arg", "--bool", "arg", "--", "--arg");
	test_one(0, 1, 0, "", "--str", "value");
	test_one(2, 0, 0, "", "--bool", "--bool2");
	test_one(2, 0, 0, "-", "--bool", "-", "--bool2");
	test_one(1, 0, 0, "--arg", "--bool", "--", "--arg");
	test_error("--nonexistent");
	test_error("--=error");
	test_one(0, 0, 0, "", "--");
	test_one(0, 0, 0, "--", "--", "--");
	test_one(2, 1, 0, "", "-abAvalue");
	test_one(2, 1, 0, "", "-abA", "value");
	test_one(2, 0, 0, "arg", "-ab", "arg");
	test_error("-A", "-ab"); // empty -A
	test_one(0, 1, 0, "", "-A-value");
	test_error("-x"); // nonexistent
	test_error("--bool", "--bool"); // more than once
	test_error("--str"); // missing argument
	test_error("--bool=error"); // argument not allowed
	test_error("--int=error"); // can't parse
	test_error("eee"); // extras not allowed here
	
	//missing-required, not tested by the above
	try
	{
		argparse args;
		args.onerror([](cstring){ throw 42; });
		string foo;
		args.add("foo", &foo).required();
		const char * const argvp[] = { "x", NULL };
		args.parse(argvp);
		assert_unreachable();
	}
	catch(int) {}
}

template<typename... Args>
static void test_getopt(bool a, bool b, const char * c, const char * nonopts, Args... argv)
{
	const char * const argvp[] = { "x", argv..., NULL };
	
	bool ra = false;
	bool rb = false;
	string rc;
	array<string> realnonopts;
	
	argparse args;
	args.add('a', "a", &ra);
	args.add('b', "b", &rb);
	args.add('c', "c", &rc);
	args.add("", &realnonopts);
	
	args.parse(argvp);
	
	assert_eq(ra, a);
	assert_eq(rb, b);
	assert_eq(rc, c);
	assert_eq(realnonopts.join("/"), nonopts);
}

test("argument parser 2","string,array","argparse")
{
	//getopt examples, https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
	//0/1 as bool because the example does so
	
	test_getopt(0, 0, NULL,  NULL);
	test_getopt(1, 1, NULL,  NULL,     "-a", "-b");
	test_getopt(1, 1, NULL,  NULL,     "-ab");
	test_getopt(0, 0, "foo", NULL,     "-c", "foo");
	test_getopt(0, 0, "foo", NULL,     "-cfoo");
	test_getopt(0, 0, NULL,  "arg1",   "arg1");
	test_getopt(1, 0, NULL,  "arg1",   "-a", "arg1");
	test_getopt(0, 0, "foo", "arg1",   "-c", "foo", "arg1");
	test_getopt(1, 0, NULL,  "-b",     "-a", "--", "-b");
	test_getopt(1, 0, NULL,  "-",      "-a", "-");
}
#endif

void arlib_init(argparse& args, char** argv)
{
	srand(time(NULL));
	arlib_init_file();
#ifndef ARGUI_NONE
	arlib_init_gui(args, argv);
#else
	args.parse(argv);
#endif
}

void arlib_init(nullptr_t, char** argv)
{
	argparse dummy;
	arlib_init(dummy, argv);
}
