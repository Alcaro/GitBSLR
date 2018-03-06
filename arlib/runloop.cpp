#include "runloop.h"
#include "set.h"
#include "test.h"

#ifdef __linux__
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

namespace {
class runloop_linux : public runloop {
public:
	#define RD_EV (EPOLLIN |EPOLLRDHUP|EPOLLHUP|EPOLLERR)
	#define WR_EV (EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLERR)
	
	int epoll_fd;
	bool exited = false;
	
#ifdef ARLIB_TEST
	bool can_exit = false;
#endif
	
	struct fd_cbs {
		function<void(uintptr_t)> cb_read;
		function<void(uintptr_t)> cb_write;
	};
	map<int,fd_cbs> fdinfo;
	
	struct timer_cb {
		unsigned id; // -1 if marked for removal
		unsigned ms;
		struct timespec next;
		function<bool()> cb;
	};
	//TODO: this should probably be a priority queue instead
	array<timer_cb> timerinfo;
	
#ifdef ARLIB_THREAD
	int submit_fds[2] = { -1, -1 };
#endif
	
	
	/*private*/ static void timespec_now(struct timespec * ts)
	{
		clock_gettime(CLOCK_MONOTONIC, ts);
	}
	
	/*private*/ static void timespec_add(struct timespec * ts, unsigned ms)
	{
		ts->tv_sec += ms/1000;
		ts->tv_nsec += (ms%1000)*1000000;
		if (ts->tv_nsec > 1000000000)
		{
			ts->tv_sec++;
			ts->tv_nsec -= 1000000000;
		}
	}
	
	//returns milliseconds
	/*private*/ static int64_t timespec_sub(struct timespec * ts1, struct timespec * ts2)
	{
		int64_t ret = (ts1->tv_sec - ts2->tv_sec) * 1000;
		ret += (ts1->tv_nsec - ts2->tv_nsec) / 1000000;
		return ret;
	}
	
	/*private*/ static bool timespec_less(struct timespec * ts1, struct timespec * ts2)
	{
		if (ts1->tv_sec < ts2->tv_sec) return true;
		if (ts1->tv_sec > ts2->tv_sec) return false;
		return (ts1->tv_nsec < ts2->tv_nsec);
	}
	
	
	
	runloop_linux()
	{
		epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	}
	
	uintptr_t set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write)
	{
		fd_cbs& cb = fdinfo.get_create(fd);
		cb.cb_read  = cb_read;
		cb.cb_write = cb_write;
		
		epoll_event ev = {}; // shut up valgrind, I only need events and data.fd, the rest of data will just come back out unchanged
		ev.events = (cb_read ? RD_EV : 0) | (cb_write ? WR_EV : 0);
		ev.data.fd = fd;
		if (ev.events)
		{
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev); // one of these two will fail (or do nothing), we'll ignore that
			epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
		}
		else
		{
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
			fdinfo.remove(fd);
		}
		return fd;
	}
	
	
	uintptr_t set_timer_rel(unsigned ms, function<bool()> callback)
	{
		unsigned timer_id = 1;
		for (size_t i=0;i<timerinfo.size();i++)
		{
			if (timerinfo[i].id == (unsigned)-1) continue;
			if (timerinfo[i].id >= timer_id)
			{
				timer_id = timerinfo[i].id+1;
			}
		}
		
		timer_cb& timer = timerinfo.append();
		timer.id = timer_id;
		timer.ms = ms;
		timer.cb = callback;
		timespec_now(&timer.next);
		timespec_add(&timer.next, ms);
		return -(intptr_t)timer_id;
	}
	
	
	void remove(uintptr_t id)
	{
		if (id == 0) return;
		
		intptr_t id_s = id;
		if (id_s >= 0)
		{
			if (!fdinfo.contains(id_s)) abort();
			
			int fd = id_s;
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
			fdinfo.remove(fd);
		}
		else
		{
			unsigned t_id = -id_s;
			for (size_t i=0;i<timerinfo.size();i++)
			{
				if (timerinfo[i].id == t_id)
				{
					timerinfo[i].id = -1;
					return;
				}
			}
			abort(); // happens if that timer doesn't exist
		}
	}
	
	
	/*private*/ void step(bool block)
	{
		struct timespec now;
		timespec_now(&now);
//printf("runloop: time is %lu.%09lu\n", now.tv_sec, now.tv_nsec);
		
		int next = INT_MAX;
		
		for (size_t i=0;i<timerinfo.size();i++)
		{
		again: ;
			timer_cb& timer = timerinfo[i];
//printf("runloop: scheduled event at %lu.%09lu\n", timer.next.tv_sec, timer.next.tv_nsec);
			
			if (timer.id == (unsigned)-1)
			{
				timerinfo.remove(i);
				
				//funny code to avoid (size_t)-1
				if (i == timerinfo.size()) break;
				goto again;
			}
			
			int next_ms = timespec_sub(&timer.next, &now);
			if (next_ms <= 0)
			{
//printf("runloop: calling event scheduled %ims ago\n", -next_ms);
				timer.next = now;
				timespec_add(&timer.next, timer.ms);
				next_ms = timer.ms;
				
				bool keep = timer.cb(); // WARNING: May invalidate 'timer'.
				if (exited) block = false; // make sure it doesn't block forever if timer callback calls exit()
				if (!keep) timerinfo[i].id = -1;
			}
			
			if (next_ms < next) next = next_ms;
		}
		
		if (next == INT_MAX) next = -1;
		if (!block) next = 0;
		
		
		epoll_event ev[16];
//printf("runloop: waiting %i ms\n", next);
		int nev = epoll_wait(epoll_fd, ev, 16, next);
		for (int i=0;i<nev;i++)
		{
			fd_cbs& cbs = fdinfo[ev[i].data.fd];
			     if ((ev[i].events & RD_EV) && cbs.cb_read)  cbs.cb_read( ev[i].data.fd);
			else if ((ev[i].events & WR_EV) && cbs.cb_write) cbs.cb_write(ev[i].data.fd);
		}
	}
	
#ifdef ARLIB_THREAD
	void prepare_submit()
	{
		if (submit_fds[0] >= 0) return;
		if (pipe2(submit_fds, O_CLOEXEC) < 0) abort();
		this->set_fd(submit_fds[0], bind_this(&runloop_linux::submit_cb), NULL);
	}
	void submit(function<void()>&& cb)
	{
		//full pipe should be impossible
		if (write(submit_fds[1], &cb, sizeof(cb)) != sizeof(cb)) abort();
		memset(&cb, 0, sizeof(cb));
	}
	/*private*/ void submit_cb(uintptr_t)
	{
		function<void()> cb;
		//we know the write pushed a complete one of those, we can assume we can read it out
		if (read(submit_fds[0], &cb, sizeof(cb)) != sizeof(cb)) abort();
		cb();
	}
#endif
	
	void enter()
	{
#ifdef ARLIB_TEST
		can_exit = true;
#endif
		exited = false;
		while (!exited) step(true);
#ifdef ARLIB_TEST
		can_exit = false;
#endif
	}
	
	void exit()
	{
#ifdef ARLIB_TEST
		assert(can_exit);
		//assert(!exited);
#endif
		exited = true;
	}
	
	void step()
	{
		step(false);
	}
	
	~runloop_linux()
	{
#ifdef ARLIB_THREAD
		if (submit_fds[0] >= 0)
		{
			//enable if I add a check that runloop is empty before destruction
			//for now, not needed
			//this->set_fd(submit_fds[0], NULL, NULL);
			close(submit_fds[0]);
			close(submit_fds[1]);
		}
#endif
		close(epoll_fd);
	}
};
}

runloop* runloop::create()
{
	runloop* ret = new runloop_linux();
#ifdef ARLIB_TEST
	ret = runloop_wrap_blocktest(ret);
#endif
	return ret;
}
#endif



uintptr_t runloop::set_timer_abs(time_t when, function<void()> callback)
{
	time_t now = time(NULL);
	unsigned ms = (now < when ? (when-now)*1000 : 0);
	return set_timer_rel(ms, bind_lambda([callback]()->bool { callback(); return false; }));
}

#ifdef ARGUI_NONE
runloop* runloop::global()
{
	//ignore thread safety, this function can only be used from main thread
	static runloop* ret = NULL;
	if (!ret) ret = runloop::create();
	return ret;
}
#endif

#include "test.h"
#include "os.h"
#include "thread.h"

#ifdef ARLIB_TEST
class runloop_blocktest : public runloop {
	runloop* loop;
	
	uint64_t us = 0;
	uint64_t loopdetect = 0;
	/*private*/ void begin()
	{
		uint64_t new_us = time_us_ne();
		if (new_us/1000000/10 != us/1000000/10) loopdetect = 0;
		loopdetect++;
		if (loopdetect == 10000) assert(!"10000 runloop iterations in 10 seconds");
		us = new_us;
	}
	/*private*/ void end()
	{
		uint64_t new_us = time_us_ne();
		uint64_t diff = new_us-us;
		_test_runloop_latency(diff);
	}
	
	//don't carelessly inline into the lambdas; sometimes lambdas are deallocated by the callbacks, so 'this' is a use-after-free
	/*private*/ void do_cb(function<void(uintptr_t)> cb, uintptr_t arg)
	{
		this->begin();
		cb(arg);
		this->end();
	}
	/*private*/ bool do_cb(function<bool()> cb)
	{
		this->begin();
		bool ret = cb();
		this->end();
		return ret;
	}
	
	uintptr_t set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write)
	{
		function<void(uintptr_t)> cb_read_w;
		function<void(uintptr_t)> cb_write_w;
		if (cb_read)  cb_read_w  = bind_lambda([this, cb_read ](uintptr_t fd){ this->do_cb(cb_read,  fd); });
		if (cb_write) cb_write_w = bind_lambda([this, cb_write](uintptr_t fd){ this->do_cb(cb_write, fd); });
		return loop->set_fd(fd, std::move(cb_read_w), std::move(cb_write_w));
	}
	
	uintptr_t set_timer_rel(unsigned ms, function<bool()> callback)
	{
		function<bool()> callback_w = bind_lambda([]()->bool { return false; });
		if (callback) callback_w = bind_lambda([this, callback]()->bool { return this->do_cb(callback); });
		return loop->set_timer_rel(ms, callback_w);
	}
	uintptr_t set_idle(function<bool()> callback)
	{
		function<bool()> callback_w = bind_lambda([]()->bool { return false; });
		if (callback) callback_w = bind_lambda([this, callback]()->bool { return this->do_cb(callback); });
		return loop->set_idle(callback_w);
	}
	void remove(uintptr_t id) { loop->remove(id); }
	void enter() { end(); loop->enter(); begin(); }
	
	void prepare_submit() { loop->prepare_submit(); }
	void submit(function<void()>&& cb) { loop->submit(std::move(cb)); }
	
	void exit() { loop->exit(); }
	void step() { end(); loop->step(); begin(); }
	
	
public:
	runloop_blocktest(runloop* inner) : loop(inner)
	{
		begin();
	}
	~runloop_blocktest()
	{
		end();
		delete loop;
	}
	
	void recycle()
	{
		us = time_us_ne();
		loopdetect = 0;
	}
};

runloop* runloop_wrap_blocktest(runloop* inner)
{
	return new runloop_blocktest(inner);
}

void runloop_blocktest_recycle(runloop* loop)
{
	((runloop_blocktest*)loop)->recycle();
}
#endif

static void test_runloop(bool is_global)
{
	runloop* loop = (is_global ? runloop::global() : runloop::create());
	
	//must be before the other one, loop->enter() must be called to ensure it doesn't actually run
	loop->remove(loop->set_timer_rel(50, bind_lambda([]()->bool { assert_ret(!"should not be called", false); return false; })));
	
	//don't put this scoped, id is used later
	uintptr_t id = loop->set_timer_rel(20, bind_lambda([&]()->bool
		{
			assert_neq_ret(id, 0, true);
			uintptr_t id_copy = id; // the 'id' reference gets freed by loop->remove(), keep a copy
			id = 0;
			loop->remove(id_copy);
			return true;
		}));
	assert_neq(id, 0); // no thinking -1 is the highest ID so 0 should be used
	
	{
		int64_t start = time_ms_ne();
		int64_t end = start;
		loop->set_timer_rel(100, bind_lambda([&]()->bool { end = time_ms_ne(); loop->exit(); return false; }));
		loop->enter();
		
		assert_range(end-start, 75,200);
	}
	
	assert_eq(id, 0);
	
#ifdef ARLIB_THREAD
	{
		loop->prepare_submit();
		uint64_t start_ms = time_ms_ne();
		function<void()> threadproc = bind_lambda([loop, start_ms]()
			{
				while (start_ms+100 > time_ms_ne()) {}
				loop->submit(bind_lambda([loop]()
					{
						loop->exit();
					}));
			});
		thread_create(threadproc);
		loop->enter();
		uint64_t end_ms = time_ms_ne();
		assert_range(end_ms-start_ms, 75,200);
	}
#endif
	
	//I could stick in some fd tests, but the sockets test all plausible operations anyways.
	//Okay, they should vary which runloop they use.
	
	if (!is_global) delete loop;
}
test("global runloop", "function,array,set,time", "runloop")
{
	test_runloop(true);
}
test("private runloop", "function,array,set,time", "runloop")
{
	test_runloop(false); // it's technically illegal to create a runloop on the main thread, but nobody's gonna notice
}
