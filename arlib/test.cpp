#ifdef ARLIB_TESTRUNNER
#ifndef ARLIB_TEST
#define ARLIB_TEST
#endif
#include "test.h"
#include "array.h"
#include "os.h"
#include "init.h"
#include "runloop.h"

struct testlist {
	void(*func)();
	
	const char * filename;
	int line;
	
	const char * name;
	
	const char * requires;
	const char * provides;
	testlist* next;
};

static testlist* g_testlist = NULL;

static testlist* cur_test;

_testdecl::_testdecl(void(*func)(), const char * filename, int line, const char * name, const char * requires, const char * provides)
{
	testlist* next = malloc(sizeof(testlist));
	next->func = func;
	next->filename = filename;
	next->line = line;
	next->name = name;
	next->requires = requires;
	next->provides = provides;
	next->next = g_testlist;
	g_testlist = next;
}

static bool all_tests;

enum err_t {
	err_ok = 0,
	err_fail = 1,
	err_skip = 2,
	err_inconclusive = 3,
	err_expfail = 4,
};
static err_t result;

static array<int> callstack;
void _teststack_push(int line) { callstack.append(line); }
void _teststack_pop() { callstack.resize(callstack.size()-1); }
static string stack(int top)
{
	if (top<0) return "";
	
	string ret = " (line "+tostring(top);
	
	for (int i=callstack.size()-1;i>=0;i--)
	{
		ret += ", called from "+tostring(callstack[i]);
	}
	
	return ret+")";
}

int nothrow_level = 0;
static void test_throw(err_t why)
{
	result = why;
	if (nothrow_level <= 0)
	{
		throw why;
	}
}

void _test_nothrow(int add)
{
	nothrow_level += add;
}

static void _testfail(cstring why)
{
	puts(why.c_str());
	fflush(stdout);
	debug_or_ignore();
	test_throw(err_fail);
}

void _testfail(cstring why, int line)
{
	_testfail(why+stack(line));
}

void _testcmpfail(cstring name, int line, cstring expected, cstring actual)
{
	if (expected.contains("\n") || actual.contains("\n") || name.length()+expected.length()+actual.length()>240)
	{
		_testfail("\nFailed assertion "+name+stack(line)+"\nexpected:\n"+expected+"\nactual:\n"+actual);
	}
	else
	{
		_testfail("\nFailed assertion "+name+stack(line)+": expected "+expected+", got "+actual);
	}
}

void _test_skip(cstring why)
{
	if (result!=err_ok) return;
	if (!all_tests)
	{
		puts("skipped: "+why);
		test_throw(err_skip);
	}
}

void _test_inconclusive(cstring why)
{
	if (result!=err_ok) return;
	puts("inconclusive: "+why);
	test_throw(err_inconclusive);
}

void _test_expfail(cstring why)
{
	if (result!=err_ok) return;
	puts("expected-fail: "+why);
	test_throw(err_expfail);
}

namespace {
struct latrec {
	uint64_t us;
	const char * name;
	const char * filename;
	int line;
	
	bool operator<(const latrec& other) { return us < other.us; }
};

latrec max_latencies_us[6];
}

void _test_runloop_latency(uint64_t us)
{
	max_latencies_us[0].us = us;
	max_latencies_us[0].name = cur_test->name;
	max_latencies_us[0].filename = cur_test->filename;
	max_latencies_us[0].line = cur_test->line;
	arrayvieww<latrec>(max_latencies_us).sort();
}


static void err_print(testlist* err)
{
	printf("%s (at %s:%i, requires %s, provides %s)\n", err->name, err->filename, err->line, err->requires, err->provides);
}

//whether 'a' must be before 'b'; alternatively, whether 'a' provides something 'b' requires
//false is not conclusive, 'a' could require being before 'c' which is before 'b'
static bool test_requires(testlist* a, testlist* b)
{
	if (!*a->requires) return false;
	if (!*b->provides) return false;
	//kinda funny code to avoid using Arlib features before they're tested
	//except strtoken, but even in the worst case, I can just revert it or set this to 'return false'
	return strtoken(a->requires, b->provides, ',');
}

//whether 'a' must be before any test in 'list'
//true if 'a' must be before itself
//actually returns pointer to the preceding test
static testlist* test_requires_any(testlist* a, testlist* list)
{
	while (list)
	{
		if (test_requires(a, list)) return list;
		list = list->next;
	}
	return NULL;
}

static testlist* sort_tests(testlist* unsorted)
{
	testlist* sorted_head = NULL;
	testlist* * sorted_tail = &sorted_head; // points to where to attach the next test, usually &something->next
	
	//ideally, this would pick all tests in a file simultaneously, if possible
	//but that's annoying to implement even with full Arlib, and this thing doesn't assume Arlib is functional
	
	while (unsorted)
	{
		bool any_here = false;
		testlist* * try_next_p = &unsorted;
		testlist* try_next = *try_next_p;
		
		while (try_next)
		{
			if (!test_requires_any(try_next, unsorted))
			{
				*try_next_p = try_next->next;
				
				*sorted_tail = try_next;
				sorted_tail = &try_next->next;
				
				try_next->next = NULL;
				
				any_here = true;
				
				try_next = *try_next_p;
			}
			else
			{
				try_next_p = &try_next->next;
				try_next = *try_next_p;
			}
		}
		if (!any_here)
		{
			puts("error: cyclical dependency");
			
			testlist* a = unsorted;
			testlist* b = unsorted;
			do
			{
				a = test_requires_any(a, unsorted);
				b = test_requires_any(b, unsorted);
				b = test_requires_any(b, unsorted);
			}
			while (a != b);
			do
			{
				err_print(a);
				a = test_requires_any(a, unsorted);
			}
			while (a != b);
			
			abort();
		}
	}
	
	return sorted_head;
}

#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#endif

#undef main // the real main is #define'd to something stupid on test runs
int main(int argc, char* argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	puts("Initializing Arlib...");
//if(argc>1)abort(); // TODO: argument parser
#ifndef ARGUI_NONE
	arlib_init(NULL, argv);
#endif
	
#ifdef __linux__
	struct rlimit rlim = { 500*1024*1024, RLIM_INFINITY };
	setrlimit(RLIMIT_AS, &rlim);
	setrlimit(RLIMIT_DATA, &rlim);
#endif
	
	all_tests = (argc>1);
	bool all_tests_twice = (argc>2);
	
	puts("Sorting tests...");
	testlist* alltests = sort_tests(g_testlist);
	
	//if running Arlib's own tests, this runs before string/array tests
	//don't care, strings/arrays work; worst case, I comment it out
	for (testlist* outer = alltests; outer; outer = outer->next)
	{
		if (outer->requires[0] == '\0') continue;
		array<cstring> required = cstring(outer->requires).csplit(",");
		for (size_t i=0;i<required.size();i++)
		{
			bool found = false;
			for (testlist* inner = alltests; inner; inner = inner->next)
			{
				if (required[i] == inner->provides)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				puts("error: dependency on nonexistent feature");
				err_print(outer);
				abort();
			}
		}
	}
	
	for (int pass = 0; pass < (all_tests_twice ? 2 : 1); pass++)
	{
		int count[5]={0};
		
		memset(max_latencies_us, 0, sizeof(max_latencies_us));
		
		cur_test = alltests;
		while (cur_test)
		{
			testlist* next = cur_test->next;
			if (cur_test->name) printf("Testing %s (%s:%i)... ", cur_test->name, cur_test->filename, cur_test->line);
			else printf("Testing %s:%i... ", cur_test->filename, cur_test->line);
			fflush(stdout);
			callstack.reset();
			result = err_ok;
			nothrow_level = 0;
			try {
				uint64_t start_time = time_us_ne();
				cur_test->func();
				uint64_t end_time = time_us_ne();
				uint64_t time_us = end_time - start_time;
				assert_lt(time_us, all_tests ? 5000*1000 : 500*1000);
			} catch (err_t e) {
				result = e;
			}
			
			if (result == err_ok) puts("pass");
			count[result]++;
			cur_test = next;
		}
		
		printf("Passed %i, failed %i", count[0], count[1]);
		if (count[2]) printf(", skipped %i", count[2]);
		if (count[3]) printf(", inconclusive %i", count[3]);
		if (count[4]) printf(", expected-fail %i", count[4]);
		puts("");
		
		for (size_t i=1;i<ARRAY_SIZE(max_latencies_us);i++)
		{
			uint64_t max_latency_us = (RUNNING_ON_VALGRIND ? 100 : 3) * 1000;
			if (max_latencies_us[i].us > max_latency_us || i == ARRAY_SIZE(max_latencies_us)-1)
			{
				if (!max_latencies_us[i].name) continue; // happens if the runloop is never used
				printf("Latency %luus at %s (%s:%i)\n",
				       (unsigned long)max_latencies_us[i].us,
				       max_latencies_us[i].name,
				       max_latencies_us[i].filename,
				       max_latencies_us[i].line);
			}
		}
		
#ifdef HAVE_VALGRIND
		if (all_tests_twice)
		{
			if (pass==0)
			{
				//discard everything leaked in first pass, it's probably gtk+ setup and whatever
				VALGRIND_DISABLE_ERROR_REPORTING;
				VALGRIND_DO_LEAK_CHECK;
				VALGRIND_ENABLE_ERROR_REPORTING;
			}
			else
			{
				VALGRIND_DO_CHANGED_LEAK_CHECK;
			}
		}
#endif
	}
	
	return 0;
}

#ifdef ARLIB_TEST_ARLIB
static int testnum = 0;
//funny order to ensure their initializers run in a funny order
test("tests themselves (1/8)", "", "test1")
{
	assert_eq(testnum, 0);
	testnum = 1;
}
test("tests themselves (2/8)", "test1", "test2")
{
	assert_eq(testnum, 1);
	testnum = 2;
}
test("tests themselves (8/8)", "test6,test7", "test8")
{
	assert_eq(testnum, 7);
	testnum = 0; // for test-all-twice
}
test("tests themselves (7/8)", "test6", "test7")
{
	assert_eq(testnum, 6);
	testnum = 7;
}
test("tests themselves (6/8)", "test5", "test6")
{
	assert_eq(testnum, 5);
	testnum = 6;
}
test("tests themselves (5/8)", "test4", "test5")
{
	assert_eq(testnum, 4);
	testnum = 5;
}
test("tests themselves (3/8)", "test2", "test3")
{
	assert_eq(testnum, 2);
	testnum = 3;
}
test("tests themselves (4/8)", "test2,test3", "test4")
{
	assert_eq(testnum, 3);
	testnum = 4;
}

test("", "", "") { test_expfail("use an argument parser"); }
#endif
#endif
