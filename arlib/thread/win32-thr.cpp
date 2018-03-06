#include "thread.h"
#if defined(_WIN32) && defined(ARLIB_THREAD)
#include <windows.h>
#include <stdlib.h>
#include <string.h>

//list of synchronization points: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686355%28v=vs.85%29.aspx

struct threaddata_win32 {
	function<void()> func;
};
static unsigned __stdcall threadproc(void* userdata)
{
	threaddata_pthread* thdat = (threaddata_pthread*)userdata;
	thdat->func();
	delete thdat;
	return 0;
}
void thread_create(function<void()> start)
{
abort();//TODO: test
	threaddata_pthread* thdat = new threaddata_pthread;
	thdat->func = start;
	
	HANDLE h = (HANDLE)_beginthreadex(NULL, 0, threadproc, thdat, 0, NULL);
	if (!h) abort();
	CloseHandle(h);
}


//static DWORD WINAPI ThreadProc(LPVOID lpParameter)
//{
//	threaddata_pthread* thdat = (threaddata_pthread*)lpParameter;
//	thdat->func();
//	delete thdat;
//	return 0;
//}
//void thread_create(function<void()> start)
//{
//	threaddata_pthread* thdat = new threaddata_pthread;
//	thdat->func = start;
//	
//	//CreateThread is not listed as a synchronization point; it probably is, but I'd rather use a pointless
//	// operation than risk a really annoying bug. It's lightweight compared to creating a thread, anyways.
//	
//	//MemoryBarrier();//gcc lacks this, and msvc lacks the gcc builtin I could use instead.
//	//And of course my gcc supports only ten out of the 137 InterlockedXxx functions. Let's pick the simplest one...
//	LONG ignored=0;
//	InterlockedIncrement(&ignored);
//	
//	HANDLE h=CreateThread(NULL, 0, ThreadProc, thdat, 0, NULL);
//	if (!h) abort();
//	CloseHandle(h);
//}

unsigned int thread_num_cores()
{
	SYSTEM_INFO sysinf;
	GetSystemInfo(&sysinf);
	return sysinf.dwNumberOfProcessors;
}

void thread_sleep(unsigned int usec)
{
	Sleep(usec/1000);
}


uintptr_t thread_get_id()
{
	//disassembly:
	//call   *0x406118
	//jmp    0x76c11427 <KERNEL32!GetCurrentThreadId+7>
	//jmp    *0x76c1085c
	//mov    %fs:0x10,%eax
	//mov    0x24(%eax),%eax
	//ret
	return GetCurrentThreadId();
}
#endif
