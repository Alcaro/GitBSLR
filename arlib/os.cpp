#define WANT_VALGRIND
#include "os.h"
#include "thread.h"
#include <stdlib.h>
#include <string.h>
#include "test.h"

#ifdef __unix__
#include <dlfcn.h>

//static mutex dylib_lock;
//
//static void* dylib_load_uniq(const char * filename, bool force)
//{
//	synchronized(dylib_lock)
//	{
//		//try loading it normally first, to avoid loading libc twice if not needed
//		//duplicate libcs probably don't work very well
//		void* test = dlopen(filename, RTLD_NOLOAD);
//		if (!test) return dlopen(filename, RTLD_LAZY);
//		
//		dlclose(test);
//#ifdef __linux__
//		if (force)
//			return dlmopen(LM_ID_NEWLM, filename, RTLD_LAZY);
//		else
//#endif
//			return NULL;
//	}
//	return NULL; // unreachable, bug in synchronized() and/or gcc
//}

bool dylib::init(const char * filename)
{
	if (handle) abort();
	//synchronized(dylib_lock)
	{
		handle = dlopen(filename, RTLD_LAZY);
	}
	return handle;
}

//bool dylib::init_uniq(const char * filename)
//{
//	if (handle) abort();
//	handle = dylib_load_uniq(filename, false);
//	return handle;
//}
//
//bool dylib::init_uniq_force(const char * filename)
//{
//	if (handle) abort();
//	handle = dylib_load_uniq(filename, true);
//	return handle;
//}

void* dylib::sym_ptr(const char * name)
{
	if (!handle) return NULL;
	return dlsym(handle, name);
}

void dylib::deinit()
{
	if (handle) dlclose(handle);
	handle = NULL;
}
#endif


#ifdef _WIN32
static mutex dylib_lock;

static HANDLE dylib_init(const char * filename, bool uniq)
{
	deinit();
	synchronized(dylib_lock)
	{
		HANDLE handle;
		
		if (uniq)
		{
			if (GetModuleHandleEx(0, filename, (HMODULE*)&handle)) return NULL;
		}
		
		//this is so weird dependencies, for example winpthread-1.dll, can be placed beside the dll where they belong
		char * filename_copy = strdup(filename);
		char * filename_copy_slash = strrchr(filename_copy, '/');
		if (!filename_copy_slash) filename_copy_slash = strrchr(filename_copy, '\0');
		filename_copy_slash[0]='\0';
		SetDllDirectory(filename_copy);
		free(filename_copy);
		
		handle = (dylib*)LoadLibrary(filename);
		SetDllDirectory(NULL);
		
		return handle;
	}
}

bool dylib::init(const char * filename, bool * owned)
{
	//not needed
}

void* dylib::sym_ptr(const char * name)
{
	if (!handle) return NULL;
	return (void*)GetProcAddress((HMODULE)handle, name);
}

void dylib::deinit()
{
	if (handle) FreeLibrary((HMODULE)handle);
	handle = NULL;
}
#endif

bool dylib::sym_multi(funcptr* out, const char * names)
{
	bool all = true;
	
	while (*names)
	{
		*out = this->sym_func(names);
		if (!*out) all = false;
		
		out++;
		names += strlen(names)+1;
	}
	
	return all;
}



#ifdef _WIN32
void debug_or_ignore()
{
	if (IsDebuggerPresent()) DebugBreak();
}

void debug_or_exit()
{
	if (IsDebuggerPresent()) DebugBreak();
	ExitProcess(1);
}

void debug_or_abort()
{
	DebugBreak();
	FatalExit(1);
}
#endif

#ifdef __unix__
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

//defined by test.h - not completely appropriate, but it is test related
#ifndef HAVE_VALGRIND
#define VALGRIND_PRINTF_BACKTRACE(...) ;
#endif

//method from https://src.chromium.org/svn/trunk/src/base/debug/debugger_posix.cc
static bool has_debugger()
{
	char buf[4096];
	int fd = open("/proc/self/status", O_RDONLY);
	if (!fd) return false;
	
	ssize_t bytes = read(fd, buf, sizeof(buf)-1);
	close(fd);
	
	if (bytes < 0) return false;
	buf[bytes] = '\0';
	
	const char * tracer = strstr(buf, "TracerPid:\t");
	if (!tracer) return false;
	tracer += strlen("TracerPid:\t");
	
	return (*tracer != '0');
}

bool debug_or_ignore()
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else if (has_debugger()) raise(SIGTRAP);
	else return false;
	return true;
}

#undef debug_or_print
#include "file.h"
bool debug_or_print(const char * filename, int line)
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else if (has_debugger()) raise(SIGTRAP);
	else
	{
		static file f;
		static mutex mut;
		synchronized(mut)
		{
			string err = (cstring)"arlib: debug_or_print("+filename+", "+tostring(line)+")\n";
			fputs(err, stderr);
			
			if (!f) f.open(file::exepath()+"/arlib-debug-or-print.log", file::m_replace);
			if (f) f.write(err);
			else fputs("arlib: debug_or_print(): couldn't open debug log", stderr);
		}
	}
	return true;
}

bool debug_or_exit()
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else if (has_debugger()) raise(SIGTRAP);
	exit(1);
}

bool debug_or_abort()
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else raise(SIGTRAP);
	abort();
}
#endif



#ifdef _WIN32
uint64_t time_us_ne()
{
	////this one has an accuracy of 10ms by default
	//ULARGE_INTEGER time;
	//GetSystemTimeAsFileTime((LPFILETIME)&time);
	//return time.QuadPart/10;//this one is in intervals of 100 nanoseconds, for some insane reason. We want microseconds.
	
	static LARGE_INTEGER timer_freq;
	if (!timer_freq.QuadPart) QueryPerformanceFrequency(&timer_freq);
	
	LARGE_INTEGER timer_now;
	QueryPerformanceCounter(&timer_now);
	return 1000000*timer_now.QuadPart/timer_freq.QuadPart;
}
#else
#include <time.h>

uint64_t time_us()
{
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return tp.tv_sec*1000000 + tp.tv_nsec/1000;
}
uint64_t time_ms()
{
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return tp.tv_sec*1000 + tp.tv_nsec/1000000;
}

uint64_t time_us_ne()
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp); // CLOCK_MONOTONIC_RAW makes more sense, but MONOTONIC uses vdso and skips the syscall
	return tp.tv_sec*1000000 + tp.tv_nsec/1000;
}
uint64_t time_ms_ne()
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec*1000 + tp.tv_nsec/1000000;
}
#endif

test("time", "", "time")
{
	uint64_t time_u_ft = (uint64_t)time(NULL)*1000000;
	uint64_t time_u_fm = time_ms()*1000;
	uint64_t time_u_fu = time_us();
	assert_range(time_u_fm, time_u_ft-1100000, time_u_ft+1100000);
	assert_range(time_u_fu, time_u_fm-1100,    time_u_fm+1500);
	
	uint64_t time_une_fm = time_ms_ne()*1000;
	uint64_t time_une_fu = time_us_ne();
	assert_range(time_une_fu, time_une_fm-1100,    time_une_fm+1500);
	
	usleep(50000);
	
	uint64_t time2_u_ft = (uint64_t)time(NULL)*1000000;
	uint64_t time2_u_fm = time_ms()*1000;
	uint64_t time2_u_fu = time_us();
	assert_range(time2_u_fm, time2_u_ft-1100000, time2_u_ft+1100000);
	assert_range(time2_u_fu, time2_u_fm-1100,    time2_u_fm+1500);
	
	uint64_t time2_une_fm = time_ms_ne()*1000;
	uint64_t time2_une_fu = time_us_ne();
	assert_range(time2_une_fu, time2_une_fm-1100,    time2_une_fm+1500);
	
	assert_range(time2_u_fm-time_u_fm, 40000, 60000);
	assert_range(time2_u_fu-time_u_fu, 40000, 60000);
	assert_range(time2_une_fm-time_une_fm, 40000, 60000);
	assert_range(time2_une_fu-time_une_fu, 40000, 60000);
}
