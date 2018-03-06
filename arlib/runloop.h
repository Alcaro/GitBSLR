#pragma once
#include "global.h"

//A runloop keeps track of a number of file descriptors, calling their handlers whenever the relevant operation is available.
//Event handlers must handle the event. If they don't, the handler may be called again forever and block everything else.
//Do not call enter() or step() while inside a callback. However, set_*(), remove() and exit() are fine.
//Like most other objects, a runloop is not thread safe.
class runloop : nocopy {
protected:
	runloop() {}
public:
	//The global runloop handles GUI events, in addition to whatever fds it's told to track. Always returns the same object.
	//The global runloop belongs to the main thread. Don't delete it, or call this function from any other thread.
	static runloop* global();
	
	//For non-primary threads. Using multiple runloops per thread is generally a bad idea.
	static runloop* create();
	
#ifndef _WIN32 // fd isn't a defined concept on windows
	//The callback argument is the fd, to allow one object to maintain multiple fds.
	//A fd can only be used once per runloop. If that fd is already there, it's removed prior to binding the new callbacks.
	//If the new callbacks are both NULL, it's removed. The return value can still safely be passed to remove().
	//If only one callback is provided, events of the other kind are ignored.
	//If both reading and writing is possible, only the read callback is called.
	//If the other side of the fd is closed, it's considered both readable and writable.
	virtual uintptr_t set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL) = 0;
#else
	//TODO: figure out sockets on windows (other fds aren't needed)
	//virtual uintptr_t set_socket(socket* sock, function<void()> cb_read, function<void()> cb_write) = 0;
#endif
	
	//Runs once.
	uintptr_t set_timer_abs(time_t when, function<void()> callback);
	//Runs repeatedly. To stop it, remove() it, return false from the callback, or both.
	//Accuracy is not guaranteed; it may or may not round the timer frequency to something it finds appropriate,
	// in either direction, and may or may not try to 'catch up' if a call is late (or early).
	//Don't use for anything that needs tighter timing than ±1 second.
	virtual uintptr_t set_timer_rel(unsigned ms, function<bool()> callback) = 0;
	
	//Will be called next time no other events (other than other idle callbacks) are ready, or earlier.
	//Like set_timer_rel, the return value tells whether to call it again later.
	virtual uintptr_t set_idle(function<bool()> callback) { return set_timer_rel(0, callback); }
	
	//Deletes an existing timer and adds a new one.
	uintptr_t set_timer_abs(uintptr_t prev_id, time_t when, function<void()> callback)
	{
		remove(prev_id);
		return set_timer_abs(when, callback);
	}
	uintptr_t set_timer_rel(uintptr_t prev_id, unsigned ms, function<bool()> callback)
	{
		remove(prev_id);
		return set_timer_rel(ms, callback);
	}
	uintptr_t set_idle(uintptr_t prev_id, function<bool()> callback)
	{
		remove(prev_id);
		return set_idle(callback);
	}
	
	//Return value from each set_*() is a token which can be used to cancel the event. remove(0) is guaranteed to be ignored.
	virtual void remove(uintptr_t id) = 0;
	
#ifdef ARLIB_THREAD
	//submit(fn) calls fn() on the runloop's thread, as soon as possible.
	//Unlike the other functions on this object, submit() may be called from a foreign thread.
	// It may also be called reentrantly, and from signal handlers.
	//Make sure there is no other reference to cb, otherwise there will be a race condition on the reference count.
	virtual void submit(function<void()>&& cb) = 0;
	//prepare_submit() must be called on the owning thread before submit() is allowed.
	//There is no way to 'unprepare' submit(), other than deleting the runloop.
	//It is safe to call prepare_submit() multiple times, even concurrently with submit().
	virtual void prepare_submit() = 0;
#endif
	
	//TODO: Slots, objects where there's no reason to have more than one per runloop (like DNS),
	// letting them be shared among runloop users
	
	//Executes the mainloop until ->exit() is called. Recommended for most programs.
	virtual void enter() = 0;
	virtual void exit() = 0;
	
	//Runs until there are no more events to process, then returns. Usable if you require control over the runloop.
	virtual void step() = 0;
	
	//Delete all runloop contents (sockets, HTTP or anything else using sockets, etc) before deleting the loop itself,
	// or said contents will use-after-free.
	// (Exception: On the non-global Linux runloop, you may have file descriptors attached during deletion.)
	//You can't remove GUI stuff from the global runloop, so you can't delete it.
	virtual ~runloop() = 0;
};
inline runloop::~runloop() {}

#ifdef ARLIB_TEST
runloop* runloop_wrap_blocktest(runloop* inner);
//used if multiple tests use the global runloop, the time spent elsewhere looks like huge runloop latency
void runloop_blocktest_recycle(runloop* loop);
#endif
