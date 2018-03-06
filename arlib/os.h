#pragma once
#include "global.h"

#ifdef __unix__
#define DYLIB_EXT ".so"
#define DYLIB_MAKE_NAME(name) "lib" name DYLIB_EXT
#endif
#ifdef _WIN32
#define DYLIB_EXT ".dll"
#define DYLIB_MAKE_NAME(name) name DYLIB_EXT
#endif
#ifdef __GNUC__
#define DLLEXPORT extern "C" __attribute__((__visibility__("default")))
#define DLLMAIN __attribute__((constructor))
#endif
#ifdef _MSC_VER
#define DLLEXPORT extern "C" __declspec(dllexport)
#endif

class dylib : nocopy {
	void* handle;
	
public:
	dylib() { handle = NULL; }
	dylib(const char * filename) { handle = NULL; init(filename); }
	
	//if called multiple times on the same object, undefined behavior, call deinit() first
	bool init(const char * filename);
	//like init, but if the library is loaded already, it fails
	//does not protect against a subsequent init() loading the same thing
	bool init_uniq(const char * filename);
	//like init, but if the library is loaded already, it loads a new instance, if supported by the platform
	bool init_uniq_force(const char * filename);
	//guaranteed to return NULL if initialization fails
	void* sym_ptr(const char * name);
	//separate function because
	//  "The ISO C standard does not require that pointers to functions can be cast back and forth to pointers to data."
	//  -- POSIX dlsym, http://pubs.opengroup.org/onlinepubs/009695399/functions/dlsym.html#tag_03_112_08
	//pretty sure the cast works fine in practice, but why not
	//compiler optimizes it out anyways
	funcptr sym_func(const char * name)
	{
		funcptr ret;
		*(void**)(&ret) = this->sym_ptr(name);
		return ret;
	}
	//TODO: do some decltype shenanigans so dylib.sym<Direct3DCreate9>("Direct3DCreate9") works
	template<typename T> T sym(const char * name) { return (T)sym_func(name); }
	
	//Fetches multiple symbols. 'names' is expected to be a NUL-separated list of names, terminated with a blank one.
	// (This is easiest done by using multiple NUL-terminated strings. The compiler appends another NUL.)
	//Returns whether all of them were successfully fetched. Failures are NULL.
	bool sym_multi(funcptr* out, const char * names);
	
	void deinit();
	~dylib() { deinit(); }
};

//If the program is run under a debugger, this triggers a breakpoint. If not, ignored.
//Returns whether it did something. The other three do too, but they always do something, if they return at all.
bool debug_or_ignore();
//If the program is run under a debugger, this triggers a breakpoint. If not, the program whines to stderr.
bool debug_or_print(const char * filename, int line);
#define debug_or_print() debug_or_print(__FILE__, __LINE__)
//If the program is run under a debugger, this triggers a breakpoint. If not, the program silently exits.
bool debug_or_exit();
//If the program is run under a debugger, this triggers a breakpoint. If not, the program crashes.
bool debug_or_abort();

//Same epoch as time(). They're unsigned because the time is known to be after 1970, but it's fine to cast them to signed.
uint64_t time_ms();
uint64_t time_us(); // this will overflow in year 586524
//No epoch; the epoch may vary across machines or reboots. May be faster. ms/us will have same epoch as each other.
uint64_t time_ms_ne();
uint64_t time_us_ne();

class timer {
	uint64_t start;
public:
	timer()
	{
		reset();
	}
	void reset()
	{
		start = time_us_ne();
	}
	uint64_t time()
	{
		return time_us_ne() - start;
	}
};
