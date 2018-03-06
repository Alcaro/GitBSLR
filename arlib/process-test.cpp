#include "process.h"
#include "test.h"

#ifdef ARLIB_THREAD
#ifndef _WIN32
#include <unistd.h> // usleep
#define TRUE "/bin/true"
#define ECHO "/bin/echo"
#define YES "/usr/bin/yes"
#define LF "\n"
#define ECHO_END LF
#define CAT_FILE "/bin/cat"
#define CAT_STDIN "/bin/cat"
#define CAT_STDIN_END ""
#else
#undef TRUE // go away windows, true !== 1 is just stupid. and so is TRUE
#define TRUE "cmd", "/c", "type NUL" // windows has no /bin/true, fake it
#define ECHO "cmd", "/c", "echo"
#define YES "cmd", "/c", "tree /f c:" // not actually infinite, but close enough
#define LF "\r\n"
#define ECHO_END LF
#define CAT_FILE "cmd", "/c", "type"
#define CAT_STDIN "find", "/v", "\"COPY_THE_INPUT_UNCHANGED\""
#define CAT_STDIN_END LF
#define usleep(n) Sleep((n)/1000)
#endif

test("process", "array,string,runloop", "process")
{
	test_skip("kinda slow");
	
	autoptr<runloop> loop = runloop::create();
	//ugly, but the alternative is nesting lambdas forever or busywait. and I need a way to break it anyways
	int status;
	function<void(int lstatus)> break_runloop = bind_lambda([&](int lstatus) { status = lstatus; loop->exit(); });
	
	{
		process p(loop);
		p.onexit(break_runloop);
		
		assert(p.launch(TRUE));
		loop->enter();
		assert_eq(status, 0);
		assert_eq(p.status(), 0);
	}
	
	{
		process p(loop);
		p.onexit(break_runloop);
		
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(ECHO, "foo"));
		
		loop->enter();
		assert_eq(status, 0);
		assert_eq(out->read(), "foo" ECHO_END);
	}
	
	{
		process p(loop);
		p.onexit(break_runloop);
		
		p.set_stdin(process::input::create_buffer("foo"));
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(CAT_STDIN));
		
		loop->enter();
		assert_eq(status, 0);
		assert_eq(out->read(), "foo" CAT_STDIN_END);
	}
	
	{
		process p(loop);
		p.onexit(break_runloop);
		
		process::input* in = p.set_stdin(process::input::create_pipe());
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(CAT_STDIN));
		in->write("foo");
		in->close();
		
		loop->enter();
		assert_eq(status, 0);
		assert_eq(out->read(), "foo" CAT_STDIN_END);
	}
	
	{
		process p(loop);
		
		process::input* in = p.set_stdin(process::input::create_pipe());
		process::output* out = p.set_stdout(process::output::create_buffer());
		
		int i = 0;
		out->callback(bind_lambda([&]()
			{
				i++;
				if (i == 10) loop->exit();
				
				if (i&1)
				{
					string str = out->read(); // it's technically allowed to give me only one byte, but PIPE_BUF says won't happen in practice
					assert_eq(str, "foo" LF);
					
					in->write("foo" LF);
				}
			}));
		
		assert(p.launch(CAT_STDIN));
		
		in->write("foo" LF);
		loop->enter();
	}
	
	{
		process p(loop);
		
		process::output* out = p.set_stdout(process::output::create_buffer());
		process::output* err = p.set_stderr(process::output::create_buffer());
		
		out->callback(bind_lambda([&]() { assert_unreachable(); }));
		err->callback(bind_lambda([&]() { assert_neq(err->read(), ""); loop->exit(); }));
		
		assert(p.launch(CAT_FILE, "nonexist.ent"));
		
		loop->enter();
	}
	
	{
		process p(loop);
		p.onexit(break_runloop);
		
		process::output* out = p.set_stdout(process::output::create_buffer());
		out->limit(1024);
		assert(p.launch(YES));
		
		loop->enter();
		string outstr = out->read();
		assert_gte(outstr.length(), 1024); // any finite number is fine
		                                   // if it ignores outlimit, it'll fail an allocation (or get OOM killer) and not reach this point
	}
	
	{
		process p(loop);
		
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(ECHO, "foo"));
		assert_eq(out->read(), ""); // RACE - it's possible, but very unlikely, that closefrom, execing echo, and whatever finishes before this
	}
	
	{
		string lots_of_data = "a" LF;
		while (lots_of_data.length() < 256*1024) lots_of_data += lots_of_data;
		
		process p(loop);
		p.onexit(break_runloop);
		
		p.set_stdin(process::input::create_buffer(lots_of_data));
		process::output* out = p.set_stdout(process::output::create_buffer());
		
		assert(p.launch(CAT_STDIN));
		
		loop->enter();
		assert_eq(out->read().length(), lots_of_data.length());
	}
	
	//no real way to test this
	//{
	//	process p;
	//	p.set_stdout(process::output::create_stdout());
	//}
	
	//{
	//	string test_escape[] = {
	//		"DUMMY_NODE",
	//		"DUMMY_NODE",
	//		"a",
	//		"\"",
	//		"a b",
	//		"\"a b\"",
	//		" ",
	//		" a",
	//		"a ",
	//		"  ",
	//		" \" ",
	//		" \"\" ",
	//		" \" \"",
	//		"",
	//		"\"",
	//		"\\",
	//		"\\\"",
	//		"\\\\",
	//		"\\\\\"",
	//		"\\\\\\",
	//		"\\\\\\\"",
	//	};
	//	//this one is supposed to test that the arguments are properly quoted,
	//	// but there's no 'dump argv' program on windows (linux doesn't need it), so can't do it
	//	//and windows has about 50 different quote parsers anyways, impossible to know which to follow
	//}
}

#ifdef ARLIB_SANDBOX
#include "sandbox/sandbox.h"

test("sandbox", "process", "sandbox")
{
	//test_skip("kinda slow");
	
	if (RUNNING_ON_VALGRIND) test_inconclusive("valgrind doesn't understand the sandbox");
	
	//use the global one instead here for no reason
	runloop* loop = runloop::global();
	//ugly, but the alternative is nesting lambdas forever or busywait. and I need a way to break it anyways
	int status;
	function<void(int lstatus)> break_runloop = bind_lambda([&](int lstatus) { status = lstatus; loop->exit(); });
	
	{
		sandproc p(loop);
		p.onexit(break_runloop);
		
		bool has_access_fail = false;
		// this will fail because can't access /lib64/ld-linux-x86-64.so.2
		// (or /bin/true or whatever - no point caring exactly what file makes it blow up)
		p.set_access_violation_cb(bind_lambda([&](cstring path, bool write) { has_access_fail = true; } ));
		//p.set_stdout(process::output::create_stdout());
		//p.set_stderr(process::output::create_stderr());
		
//puts("");
//printf("%lu\n",time_us_ne());
		assert(p.launch(TRUE));
//printf("%lu\n",time_us_ne());
		loop->enter();
		
		assert(has_access_fail);
		assert_neq(status, 0);
	}
	
	{
		sandproc p(loop);
		p.onexit(break_runloop);
		
		p.set_access_violation_cb(bind_lambda([&](cstring path, bool write) { assert_unreachable(); } ));
		//p.set_stdout(process::output::create_stdout());
		//p.set_stderr(process::output::create_stderr());
		p.fs_grant_syslibs(TRUE);
		
//printf("%lu\n",time_us_ne());
		assert(p.launch(TRUE));
//printf("%lu\n",time_us_ne());
		loop->enter();
		
		assert_eq(status, 0);
	}
}
#endif
#endif
