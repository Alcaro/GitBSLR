#pragma once
#include "../global.h"

#include "atomic.h"

#ifdef ARLIB_THREAD
//Any data associated with this thread is freed once the thread procedure returns.
//It is safe to malloc() something in one thread and free() it in another.
//It is not safe to call window_run_*() from a thread other than the one entering main().
//A thread is rather heavy; for short-running jobs, use thread_create_short or thread_split.
void thread_create(function<void()> start);

////Returns the number of threads to create to utilize the system resources optimally.
//unsigned int thread_num_cores();

#include <string.h>
#if defined(__unix__)
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#endif

//This is a simple tool that ensures only one thread is doing a certain action at a given moment.
//Memory barriers are inserted as appropriate. Any memory access done while holding a lock is finished while holding this lock.
//This means that if all access to an object is done exclusively while holding the lock, no further synchronization is needed.
//It is not allowed for a thread to call lock() or try_lock() while holding the lock already. It is not allowed
// for a thread to release the lock unless it holds it. It is not allowed to delete the lock while it's held.
//However, it it allowed to hold multiple locks simultaneously.
//lock() is not guaranteed to yield the CPU if it can't grab the lock. It may be implemented as a
// busy loop, or a hybrid scheme that spins a few times and then sleeps.
//Remember to create all relevant mutexes before creating a thread.
class mutex : nocopy {
protected:
#if defined(__unix__)
	pthread_mutex_t mut;
	
	class noinit {};
	mutex(noinit) {}
public:
	mutex() { pthread_mutex_init(&mut, NULL); }
	void lock() { pthread_mutex_lock(&mut); }
	bool try_lock() { return pthread_mutex_trylock(&mut); }
	void unlock() { pthread_mutex_unlock(&mut); }
	~mutex() { pthread_mutex_destroy(&mut); }
	
#elif _WIN32_WINNT >= 0x0600
#if !defined(_MSC_VER) || _MSC_VER > 1600
	SRWLOCK srwlock = SRWLOCK_INIT;
#else
	// apparently MSVC2008 doesn't understand struct S item = {0}. let's do something it does understand and hope it's optimized out.
	SRWLOCK srwlock;
public:
	mutex() { srwlock.Ptr = NULL; } // and let's hope MS doesn't change the definition of RTL_SRWLOCK.
#endif
	//I could define a path for Windows 8+ that uses WaitOnAddress to shrink it to one single byte, but
	//(1) The more code paths, the more potential for bugs, especially the code paths I don't regularly test
	//(2) Saving seven bytes is pointless, a mutex is for protecting other resources and they're bigger
	//(3) Microsoft's implementation is probably better optimized
	//(4) I can't test it without a machine running 8 or higher, and I don't have that
	
public:
	void lock() { AcquireSRWLockExclusive(&srwlock); }
	bool try_lock() { return TryAcquireSRWLockExclusive(&srwlock); }
	void unlock() { ReleaseSRWLockExclusive(&srwlock); }
	
#elif _WIN32_WINNT >= 0x0501
	
	CRITICAL_SECTION cs;
	
public:
	mutex() { InitializeCriticalSection(&cs); }
	void lock() { EnterCriticalSection(&cs); }
	bool try_lock() { return TryEnterCriticalSection(&cs); }
	void unlock() { LeaveCriticalSection(&cs); }
	~mutex() { DeleteCriticalSection(&cs); }
#endif
};


class mutex_rec : public mutex {
#if defined(__unix__)
public:
	mutex_rec();
#else
private: // unimplemented
	mutex_rec();
#endif
};


class mutexlocker : nocopy {
	mutexlocker();
	mutex* m;
public:
	mutexlocker(mutex* m) { this->m = m;  this->m->lock(); }
	mutexlocker(mutex& m) { this->m = &m; this->m->lock(); }
	void unlock() { if (this->m) this->m->unlock(); this->m = NULL; }
	~mutexlocker() { unlock(); }
};
#define synchronized(mutex) using(mutexlocker LOCK(mutex))


class semaphore : nomove {
#ifdef __unix__
	sem_t sem;
public:
	semaphore() { sem_init(&sem, false, 0); }
	void release() { sem_post(&sem); }
	void wait() { while (sem_wait(&sem)<0 && errno==EINTR) {} } // why on earth is THIS one interruptible
	~semaphore() { sem_destroy(&sem); }
#endif
#ifdef _WIN32
	HANDLE sem;
public:
	semaphore() { sem = CreateSemaphore(NULL, 0, 1000, NULL); }
	void release() { ReleaseSemaphore(sem, 1, NULL); }
	void wait() { WaitForSingleObject(sem, INFINITE); }
	~semaphore() { CloseHandle(sem); }
#endif
};


class runonce : nomove {
	typedef void (*once_fn_t)(void);
#ifdef __unix__
	pthread_once_t once = PTHREAD_ONCE_INIT;
public:
	void run(once_fn_t fn) { pthread_once(&once, fn); }
#endif
#ifdef _WIN32
#if _WIN32_WINNT >= 0x0600
	INIT_ONCE once = INIT_ONCE_STATIC_INIT;
	//strangely enough, pthread is the lowest common denominator here
	static BOOL CALLBACK wrap(INIT_ONCE* once, void* param, void** context)
	{
		(*(once_fn_t*)param)();
		return TRUE;
	}
public:
	void run(once_fn_t fn)
	{
		InitOnceExecuteOnce(&once, wrap, &fn, NULL);
	}
#else
#error no XP support
#endif
#endif
};
#define RUN_ONCE(fn) do { static runonce once; once.run(fn); } while(0);
#define RUN_ONCE_FN(name) static void name##_core(); static void name() { RUN_ONCE(name##_core); } static void name##_core()


//void thread_sleep(unsigned int usec);

#ifdef __unix__
static inline size_t thread_get_id() { return pthread_self(); }
#endif
#ifdef _WIN32
static inline size_t thread_get_id() { return GetCurrentThreadId(); }
#endif

//This one creates 'count' threads, calls work() in each of them with 'id' from 0 to 'count'-1, and
// returns once each thread has returned.
//Unlike thread_create, thread_split is expected to be called often, for short-running tasks. The threads may be reused.
//It is safe to use the values 0 and 1. However, you should avoid going above thread_ideal_count().
void thread_split(unsigned int count, function<void(unsigned int id)> work);


//It is permitted to define this as (e.g.) QThreadStorage<T> rather than compiler magic.
//However, it must support operator=(T) and operator T(), so QThreadStorage is not directly usable. A wrapper may be.
//An implementation must support all stdint.h types, all basic integral types (char, short, etc), and all pointers.
#ifdef __GNUC__
#define THREAD_LOCAL(t) __thread t
#endif
#ifdef _MSC_VER
#define THREAD_LOCAL(t) __declspec(thread) t
#endif


#ifdef __linux__
//sleeps if *uaddr == val
int futex_wait(int* uaddr, int val, const struct timespec * timeout = NULL);
int futex_wake(int* uaddr);
int futex_wake_all(int* uaddr);

//convenience wrappers
void futex_wait_while_eq(int* uaddr, int val);
void futex_set_and_wake(int* uaddr, int val);
#endif

#else

//Some parts of Arlib want to work with threads disabled.
class mutex : nocopy {
public:
	void lock() {}
	bool try_lock() { return true; }
	void unlock() { }
};
#define RUN_ONCE(fn) do { static bool first=true; if (first) fn(); first=false; } while(0)
#define RUN_ONCE_FN(name) static void name##_core(); static void name() { RUN_ONCE(name##_core); } static void name##_core()
#define synchronized(mutex)
static inline size_t thread_get_id() { return 0; }

#endif
