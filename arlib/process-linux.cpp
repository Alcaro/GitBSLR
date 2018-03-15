#include "process.h"

#ifdef __linux__
#include <spawn.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <poll.h>
#include "set.h"
#include "file.h"

namespace sigchld {
	static int pipe[2] = {-1, -1};
	
	static mutex mut;
	//contains NULL if the pid died before being added here
	static map<pid_t, process*> owners;
	
	static void handler_nat(int signo, siginfo_t* info, void* context)
	{
		int bytes = write(pipe[1], &info->si_pid, sizeof(pid_t));
		if (bytes != sizeof(pid_t)) abort();
	}
	
	static void on_readable(uintptr_t pipe_0)
	{
		//keep this outside the mutex, no need to race on it if we'd just get EAGAIN
		//pipe_0 is known equal to pipe[0]
		pid_t pid;
		int bytes = read(pipe_0, &pid, sizeof(pid));
		if (bytes == -1 && errno == EAGAIN) return;
		if (bytes != sizeof(pid)) abort();
		
		synchronized(mut)
		{
			process* proc = owners.get_or(pid, NULL);
			if (proc)
			{
				proc->_on_sigchld_offloop();
				owners.remove(pid);
			}
			else
			{
				owners.insert(pid, NULL);
			}
		}
	}
	
//public:
	//this must be called before the pid exists
	static void init(runloop* loop)
	{
		synchronized(mut)
		{
			if (pipe[0] < 0)
			{
				if (pipe2(pipe, O_CLOEXEC|O_NONBLOCK) < 0) abort();
				
				//do this before writing sigchld_pipe
				struct sigaction act;
				act.sa_sigaction = handler_nat;
				sigemptyset(&act.sa_mask);
				//unbounded recursion (SA_NODEFER) is bad, but the alternative is trying to waitpid every single child as soon as one dies
				//and it's not unbounded anyways, it's bounded by number of child processes, which won't go over ~1000 and stack can handle that.
				act.sa_flags = SA_SIGINFO|SA_NODEFER|SA_NOCLDSTOP;
				sigaction(SIGCHLD, &act, NULL);
			}
#ifdef ARLIB_THREAD
			loop->prepare_submit();
#endif
			loop->set_fd(pipe[0], on_readable, NULL);
		}
	}
	
	static void listen(pid_t pid, process* proc)
	{
		synchronized(mut)
		{
			if (owners.contains(pid))
			{
				proc->_on_sigchld_offloop();
				owners.remove(pid);
			}
			else
			{
				owners.insert(pid, proc);
			}
		}
	}
	
	static void process()
	{
		//reading pipe[0] without holding the mutex is safe, it's only written by init() which has been called long ago
		struct pollfd pfd = { pipe[0], POLLIN, 0 };
		poll(&pfd, 1, 1); // timeout to ensure no glitches if some other runloop got here first
		on_readable(pipe[0]);
	}
}


//atoi is locale-aware, not gonna trust that to not call malloc or otherwise be stupid
static int atoi_simple(const char * text)
{
	int ret = 0; // ignore overflows, fds don't go higher than a couple thousand before the kernel gets angry at you
	while (*text)
	{
		if (*text<'0' || *text>'9') return -1;
		ret *= 10;
		ret += *text-'0';
		text++;
	}
	return ret;
}

//trusting everything to set O_CLOEXEC isn't enough, this is a sandbox
bool process::closefrom(int lowfd)
{
	//getdents[64] is documented do-not-use and opendir should be used instead.
	//However, we're in (the equivalent of) a signal handler, and opendir is not signal safe.
	//Therefore, raw kernel interface it is.
	struct linux_dirent64 {
		ino64_t        d_ino;    /* 64-bit inode number */
		off64_t        d_off;    /* 64-bit offset to next structure */
		unsigned short d_reclen; /* Size of this dirent */
		unsigned char  d_type;   /* File type */
		char           d_name[]; /* Filename (null-terminated) */
	};
	
	int dfd = open("/proc/self/fd/", O_RDONLY|O_DIRECTORY);
	if (dfd < 0) return false;
	
	while (true)
	{
		char bytes[1024];
		// this interface always returns full structs, no need to paste things together
		int nbytes = syscall(SYS_getdents64, dfd, &bytes, sizeof(bytes));
		if (nbytes < 0) { close(dfd); return false; }
		if (nbytes == 0) break;
		
		int off = 0;
		while (off < nbytes)
		{
			linux_dirent64* dent = (linux_dirent64*)(bytes+off);
			off += dent->d_reclen;
			
			int fd = atoi_simple(dent->d_name);
			if (fd>=0 && fd!=dfd && fd>=lowfd)
			{
#ifdef ARLIB_TEST_ARLIB
				if (fd >= 1024) continue; // shut up, Valgrind
#endif
				close(fd); // seems like close() can't fail, per https://lwn.net/Articles/576478/
			}
		}
	}
	close(dfd);
	return true;
}

bool process::set_fds(arrayvieww<int> fds, bool cloexec)
{
	if (fds.size() > INT_MAX) return false;
	
	bool ok = true;
	
	//probably doable with fewer dups, but yawn, don't care.
	for (size_t i=0;i<fds.size();i++)
	{
		while (fds[i] < (int)i && fds[i] >= 0)
		{
			fds[i] = fcntl(fds[i], F_DUPFD_CLOEXEC);
			if (fds[i] < 0) ok = false;
		}
	}
	
	for (size_t i=0;i<fds.size();i++)
	{
		if (fds[i] >= 0)
		{
			if (fds[i] != (int)i)
			{
				if (dup3(fds[i], i, O_CLOEXEC) < 0)
				{
					ok = false;
					close(i);
				}
			}
			fcntl(i, F_SETFD, cloexec ? FD_CLOEXEC : 0);
		}
		if (fds[i] < 0) close(i);
	}
	
	if (!closefrom(fds.size())) ok = false;
	
	return ok;
}

string process::find_prog(cstring prog)
{
	if (prog.contains("/")) return prog;
	array<string> paths = string(getenv("PATH")).split(":");
	for (string& path : paths)
	{
		string pathprog = path+"/"+prog;
		if (access(pathprog, X_OK)==0) return pathprog;
	}
	return "";
}


struct process::onexit_t {
	bool called;
	short refcount;
	int arg;
	function<void(int)> callback;
	
	void ref()
	{
		lock_incr(&refcount);
	}
	void invoke_unref() // not thread safe
	{
		if (!called)
		{
			callback(arg);
			called = true;
		}
		
		if (--refcount == 0) delete this;
	}
};

pid_t process::launch_impl(array<const char*> argv, array<int> stdio_fd)
{
	pid_t ret = fork();
	if (ret == 0)
	{
		//WARNING:
		//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
		//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
		//  calling thread and its entire address space, possibly including the states of mutexes and
		//  other resources. Consequently, to avoid errors, the child process may only execute
		//  async-signal-safe operations until such time as one of the exec functions is called.
		//In particular, malloc must be avoided.
		
		set_fds(stdio_fd);
		execvp(argv[0], (char**)argv.ptr());
		while (true) _exit(EXIT_FAILURE);
	}
	
	return ret;
}

bool process::launch(cstring prog, arrayview<string> args)
{
	sigchld::init(loop);
	
	array<const char*> argv;
	string progpath = find_prog(prog); // don't inline, the variable must be kept alive until calling launch_impl
	if (!progpath) return false;
	argv.append((const char*)progpath);
	for (size_t i=0;i<args.size();i++)
	{
		argv.append((const char*)args[i]);
	}
	argv.append(NULL);
	
	if (ch_stdin ) ch_stdin ->init(loop);
	if (ch_stdout) ch_stdout->init(loop);
	if (ch_stderr) ch_stderr->init(loop);
	
	array<int> fds;
	fds.append(ch_stdin  ? ch_stdin ->pipe[0] : open("/dev/null", O_RDONLY));
	fds.append(ch_stdout ? ch_stdout->pipe[1] : open("/dev/null", O_WRONLY));
	fds.append(ch_stderr ? ch_stderr->pipe[1] : open("/dev/null", O_WRONLY));
	
	
	//can poke this->pid/exitcode freely before calling sigchld::listen
	this->pid = launch_impl(std::move(argv), fds);
	
	if (ch_stdin)
	{
		ch_stdin->started = true;
		ch_stdin->update();
	}
	
	close(fds[0]);
	close(fds[1]);
	close(fds[2]);
	
	if (this->pid < 0)
	{
		this->exitcode = 127; // the usual linux 'couldn't exec' error
		return false;
	}
	
	sigchld::listen(this->pid, this);
	
	return true;
}

void process::terminate()
{
	if (running())
	{
		kill(this->pid, SIGKILL);
		while (running()) sigchld::process();
		//at this point, _on_sigchld_offloop is known to have returned
	}
	if (onexit_cb)
	{
		onexit_cb->arg = status();
		onexit_cb->invoke_unref();
		onexit_cb = NULL;
	}
}

//threading here is fairly unusual, but not too tricky to deal with
//- this one can be called on any thread, but only once
//- terminate() may be running simultaneously
//- once terminate() returns, the object may stop existing
//most evil case (which doesn't even require threads):
//- terminate() is called
//- this one ends up called
//- a call to onexit is scheduled
//- terminate returns
//- onexit call is done - on a now dead object
//therefore, onexit callback must be a separate allocation that outlives the process
void process::_on_sigchld_offloop()
{
	//this must be off-loop, the correct runloop could be stuck in process::terminate()
	int status;
	waitpid(this->pid, &status, 0);
	
	if (onexit_cb)
	{
		onexit_cb->ref();
		onexit_cb->arg = status;
#ifdef ARLIB_THREAD
		this->loop->submit(bind_ptr(&onexit_t::invoke_unref, onexit_cb));
#else
		//ensure exitcode is written before onexit is called
		lock_write(&this->exitcode, status);
		lock_write(&this->pid, -1);
		onexit_cb->invoke_unref(); // if Arlib is configured single threaded, we're always on the right thread, so this is safe
		return; // no point writing twice
#endif
	}
	
	lock_write(&this->exitcode, status);
	lock_write(&this->pid, -1);
}

void process::onexit(function<void(int)> cb)
{
	if (!onexit_cb)
	{
		onexit_cb = new onexit_t;
		onexit_cb->called = false;
		onexit_cb->refcount = 1;
	}
	onexit_cb->callback = cb;
}

process::~process()
{
	terminate();
	delete ch_stdin;
	delete ch_stdout;
	delete ch_stderr;
}



void process::input::init(runloop* loop)
{
	this->loop = loop;
	update();
}
void process::input::update(uintptr_t)
{
	if (pipe[1] == -1) return;
	
	if (buf.remaining() != 0)
	{
		arrayview<byte> bytes = buf.pull_buf();
		ssize_t nbytes = ::write(pipe[1], bytes.ptr(), bytes.size());
		if (nbytes < 0 && errno == EAGAIN) goto do_monitor;
		if (nbytes <= 0) goto do_terminate;
		buf.pull_done(nbytes);
	}
	if (buf.remaining() == 0 && started && do_close) goto do_terminate;
	
do_monitor:
	bool should_monitor;
	should_monitor = (buf.remaining() != 0);
	if (should_monitor != monitoring)
	{
		loop->set_fd(pipe[1], NULL, (should_monitor ? bind_this(&input::update) : NULL));
		this->monitoring = should_monitor;
	}
	return;
	
do_terminate:
	terminate();
	return;
}

process::input& process::input::create_pipe(arrayview<byte> data)
{
	input* ret = new input();
	if (pipe2(ret->pipe, O_CLOEXEC) < 0) abort();
	fcntl(ret->pipe[1], F_SETFL, fcntl(ret->pipe[1], F_GETFL, 0) | O_NONBLOCK);
	ret->buf.push(data);
	return *ret;
}
process::input& process::input::create_buffer(arrayview<byte> data)
{
	input& ret = create_pipe(data);
	ret.do_close = true;
	return ret;
}

//process::input& process::input::create_file(cstring path)

//dup because process::launch closes it
process::input& process::input::create_stdin() { return *new input(fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0)); }

void process::input::terminate()
{
	if (pipe[1] != -1)
	{
		::close(pipe[1]);
		pipe[1] = -1;
	}
}
process::input::~input() { terminate(); }



void process::output::init(runloop* loop)
{
	if (pipe[0] != -1) loop->set_fd(pipe[0], bind_this(&output::update_with_cb), NULL);
	this->loop = loop;
}
void process::output::callback(function<void()> cb)
{
	this->on_readable = cb;
	update();
}
void process::output::update()
{
	if (pipe[0] != -1)
	{
		uint8_t tmp[65536];
		ssize_t bytes = ::read(pipe[0], tmp, sizeof(tmp));
		if (bytes < 0 && errno == EAGAIN) return;
		if (bytes <= 0) return terminate();
		buf += arrayview<byte>(tmp, bytes);
		if (buf.size() >= maxbytes) return terminate();
	}
}
void process::output::update_with_cb(uintptr_t)
{
	update();
	while (on_readable && buf.size() != 0) on_readable();
}

void process::output::terminate()
{
	if (pipe[0] != -1)
	{
		loop->set_fd(pipe[0], NULL);
		close(pipe[0]);
		pipe[0] = -1;
	}
}

process::output& process::output::create_buffer(size_t limit)
{
	output* ret = new output();
	if (pipe2(ret->pipe, O_CLOEXEC) < 0) abort();
	fcntl(ret->pipe[0], F_SETFL, fcntl(ret->pipe[0], F_GETFL, 0) | O_NONBLOCK);
	ret->maxbytes = limit;
	return *ret;
}

//process::output& process::output::create_file(cstring path, bool append)

process::output& process::output::create_stdout() { return *new output(fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0)); }
process::output& process::output::create_stderr() { return *new output(fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0)); }
#endif
