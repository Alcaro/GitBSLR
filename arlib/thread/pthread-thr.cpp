#include "thread.h"
#if defined(__unix__)
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//list of synchronization points: http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap04.html#tag_04_10

struct threaddata_pthread {
	function<void()> func;
};
static void * threadproc(void * userdata)
{
	threaddata_pthread* thdat = (threaddata_pthread*)userdata;
	thdat->func();
	delete thdat;
	return NULL;
}

void thread_create(function<void()> start)
{
	threaddata_pthread* thdat = new threaddata_pthread;
	thdat->func = start;
	pthread_t thread;
	if (pthread_create(&thread, NULL, threadproc, thdat) != 0) abort();
	pthread_detach(thread);
}

mutex_rec::mutex_rec() : mutex(noinit())
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mut, &attr);
	pthread_mutexattr_destroy(&attr);
}


//unsigned int thread_num_cores()
//{
//	//for more OSes: https://qt.gitorious.org/qt/qt/source/HEAD:src/corelib/thread/qthread_unix.cpp#L411, idealThreadCount()
//	//or http://stackoverflow.com/questions/150355/programmatically-find-the-number-of-cores-on-a-machine
//	return sysconf(_SC_NPROCESSORS_ONLN);
//}
#endif
