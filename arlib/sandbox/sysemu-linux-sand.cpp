#ifdef SANDBOX_INTERNAL

//#define _GNU_SOURCE // default (mandatory) in c++
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
#include <errno.h>
#include <sched.h>

#include "syscall.h"
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/utsname.h>

//gcc recognizes various function names and reads attributes (such as extern) from the headers, force it not to
namespace mysand { namespace {
#undef errno

static const char * progname;


class mutex {
public:
	void lock()
	{
		//TODO (copy arlib/thread/linux.cpp)
	}
	void unlock()
	{
	}
};
class mutexlocker {
	mutexlocker();
	mutex* m;
public:
	mutexlocker(mutex* m) { this->m = m;  this->m->lock(); }
	~mutexlocker() { this->m->unlock(); }
};


//have to redefine the entire libc, fun
//(there's some copying between this and preload.cpp)
static inline void memset(void* ptr, int value, size_t num)
{
	//compiler probably optimizes this
	uint8_t* ptr_ = (uint8_t*)ptr;
	for (size_t i=0;i<num;i++) ptr_[i] = value;
}
static inline void memcpy(void * dest, const void * src, size_t n)
{
	uint8_t* dest_ = (uint8_t*)dest;
	uint8_t* src_ = (uint8_t*)src;
	for (size_t i=0;i<n;i++) dest_[i] = src_[i];
}
static inline int memcmp(const void * ptr1, const void * ptr2, size_t n)
{
	uint8_t* ptr1_ = (uint8_t*)ptr1;
	uint8_t* ptr2_ = (uint8_t*)ptr2;
	for (size_t i=0;i<n;i++)
	{
		if (ptr1_[i] != ptr2_[i]) return ptr1_[i]-ptr2_[i];
	}
	return 0;
}
static inline void strcpy(char * dest, const char * src)
{
	while (*src) *dest++ = *src++;
	*dest = '\0';
}
static inline size_t strlen(const char * str)
{
	const char * iter = str;
	while (*iter) iter++;
	return iter-str;
}

static inline int close(int fd)
{
	return syscall1(__NR_close, fd);
}

static inline void exit_group(int status)
{
	syscall1(__NR_exit_group, status);
	__builtin_unreachable();
}

static inline ssize_t write(int fd, const void * buf, size_t count)
{
	return syscall3(__NR_write, fd, (long)buf, count);
}

#define fstat fstat_ // gcc claims a few syscalls are ambiguous, the outer (extern) one being as good as this one
static inline int fstat(int fd, struct stat * buf)
{
	return syscall2(__NR_fstat, fd, (long)buf);
}

static inline int fchmod(int fd, mode_t mode)
{
	return syscall2(__NR_fchmod, fd, mode);
}

#define send send_
static inline ssize_t send(int sockfd, const void * buf, size_t len, int flags)
{
	return syscall6(__NR_sendto, sockfd, (long)buf, len, flags, (long)NULL, 0); // no send syscall
}

static inline int dup2(int oldfd, int newfd)
{
	return syscall2(__NR_dup2, oldfd, newfd);
}

#define recvmsg recvmsg_
static inline ssize_t recvmsg(int sockfd, struct msghdr * msg, int flags)
{
	return syscall3(__NR_recvmsg, sockfd, (long)msg, flags);
}

static inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void*)syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

static inline int fcntl(unsigned int fd, unsigned int cmd)
{
	return syscall2(__NR_fcntl, fd, cmd);
}
static inline int fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return syscall3(__NR_fcntl, fd, cmd, arg);
}


//TODO: free()
void* malloc(size_t size)
{
	//what's the point of brk()
	void* ret = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if ((unsigned long)ret >= (unsigned long)-4095) return NULL;
	return ret;
}


//basically my printf
inline void print1(int fd, size_t y)
{
	char buf[40];
	char* bufend = buf+40;
	char* bufat = bufend;
	while (y)
	{
		*--bufat = '0'+(y%10);
		y /= 10;
	}
	if (bufend==bufat) *--bufat='0';
	write(fd, bufat, bufend-bufat);
}
inline void print1x(int fd, size_t y)
{
	static const char digits[] = "0123456789ABCDEF";
	char buf[16];
	for (int i=0;i<16;i++)
	{
		buf[15-i] = digits[(y>>(i*4))&15];
	}
	write(fd, buf, 16);
}
inline void print1(int fd, const char * x)
{
	write(fd, x, strlen(x));
}
inline void print(int fd) {}
class hex {};
template<typename T, typename... Tnext> inline void print(int fd, T first, Tnext... next)
{
	print1(fd, first);
	print(fd, next...);
}
template<typename T, typename... Tnext> inline void print(int fd, hex n, T first, Tnext... next)
{
	print1x(fd, first);
	print(fd, next...);
}
template<typename... Ts> static inline void error(Ts... ts)
{
	print(2, progname, ": ", ts..., "\n");
}


#include "internal-linux-sand.h"
#define FD_PARENT 3
static mutex broker_mut;
static inline int do_broker_req(broker_req* req)
{
	mutexlocker l(&broker_mut);
	
	send(FD_PARENT, req, sizeof(*req), MSG_EOR);
	
	broker_rsp rsp;
	int fd;
	ssize_t len = recv_fd(FD_PARENT, &rsp, sizeof(rsp), MSG_CMSG_CLOEXEC, &fd);
	if (len != sizeof(rsp))
	{
		error("socket returned invalid data");
		exit_group(1);
	}
	if (fd >= 0) return fd;
	else if (rsp.err==0) return 0;
	else return -rsp.err;
}

static char cwd[SAND_PATHLEN];
//not sure about the return type, glibc uses int despite buflen being size_t. guess they don't care about 2GB paths
//then neither do I
static inline int getcwd(char * buf, size_t size)
{
	size_t outsize = strlen(cwd)+1;
	if (outsize > size) outsize = size;
	memcpy(buf, cwd, outsize);
	return outsize;
}

//false for overflow
static bool flatten_path(const char * path, char * outpath, size_t outlen)
{
	char* out_end = outpath+outlen;
	char* outat = outpath;
	if (path[0] != '/')
	{
		int cwdlen = getcwd(outat, out_end-outat);
		if (cwdlen <= 0) return false;
		outat += cwdlen-1;
	}
	*outat++ = '/';
	
	while (*path)
	{
		const char * component = path;
		bool dots = true;
		while (*path!='/' && *path!='\0')
		{
			if (*path!='.') dots=false;
			path++;
		}
		int complen = path-component;
		if (*path) path++;
		
		if (dots && complen==0) continue;
		if (dots && complen==1) continue;
		if (dots && complen==2)
		{
			if (outat != outpath+1)
			{
				outat--;
				while (outat[-1]!='/') outat--;
			}
			continue;
		}
		
		memcpy(outat, component, complen);
		outat += complen;
		
		if (*path) *outat++='/';
		else *outat='\0';
	}
	
	return true;
}

static inline int chdir(const char * path)
{
	char buf[SAND_PATHLEN];
	if (!flatten_path(path, buf, sizeof(buf))) return -ENOENT;
	strcpy(cwd, buf);
	return 0;
}

static inline int do_broker_file_req(const char * pathname, broker_req_t op, int flags1 = 0, mode_t flags2 = 0)
{
	broker_req req = { op, { (uint32_t)flags1, flags2 } };
	if (!flatten_path(pathname, req.path, sizeof(req.path))) return -ENOENT;
	return do_broker_req(&req);
}

static inline int open(const char * pathname, int flags, mode_t mode = 0)
{
//error("open: ", pathname);
	int fd = do_broker_file_req(pathname, br_open, flags, mode);
	if (fd>=0 && !(flags&O_CLOEXEC)) fcntl(fd, F_SETFD, fcntl(fd, F_GETFD)&~O_CLOEXEC);
//if (fd >= 0) error("open: ", pathname, " = ", fd);
//else error("open: ", pathname, " = -", -fd);
	return fd;
}

static inline int openat(int dirfd, const char * pathname, int flags, mode_t mode = 0)
{
	if (dirfd == AT_FDCWD) return open(pathname, flags, mode);
	
	error("denied syscall openat ", pathname);
	return -ENOENT;
}

static inline int unlink(const char * pathname)
{
	return do_broker_file_req(pathname, br_unlink);
}

static inline int access(const char * pathname, int flags)
{
	return do_broker_file_req(pathname, br_access, flags);
}

//explicit underscore because #define stat stat_ messes up the struct
static inline int stat_(const char * pathname, struct stat * buf)
{
	int fd = open(pathname, O_RDONLY);
	if (fd<0) return fd;
	
	int ret = fstat(fd, buf);
	close(fd);
	return ret;
}

static inline int chmod(const char * pathname, mode_t mode)
{
	int fd = open(pathname, O_RDONLY);
	if (fd<0) return fd;
	
	int ret = fchmod(fd, mode);
	close(fd);
	return ret;
}

static inline pid_t clone(unsigned long clone_flags, unsigned long newsp,
                          int* parent_tidptr, int* child_tidptr, unsigned long tls)
{
	//the bpf filter allows clone with CLONE_THREAD set, so this doesn't block any usual use of SETTID
	if (clone_flags&CLONE_PARENT_SETTID) return -EINVAL;
	
	//glibc fork() uses CHILD_{SET,CLEAR}TID to ensure a first-instruction interrupt handler getpid() is accurate
	//PARENT_SETTID is unused, so we hijack it
	broker_req req = { br_fork };
	int fd = do_broker_req(&req);
	if (fd<0) return -ENOMEM; // probably accurate enough
	
	unsigned long required_flags = SIGCHLD|CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID|CLONE_PARENT_SETTID;
	if (!(clone_flags&(CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID)))
	{
		child_tidptr = NULL;
	}
	pid_t ret = syscall5(__NR_clone, required_flags, newsp, (long)NULL, (long)child_tidptr, tls);
	//pid_t ret = syscall5(__NR_clone, clone_flags, newsp, (long)parent_tidptr, (long)0x12345678, tls);
	if (ret<0)
	{
		//couldn't fork
		close(fd);
		return ret;
	}
	else if (ret==0)
	{
		//child
		dup2(fd, FD_PARENT);
		close(fd);
		return ret;
	}
	else // ret>0
	{
		//parent
		close(fd);
		return ret;
	}
}

static inline const char * * dup_prefix(const char * const * item, const char* prefix)
{
	int n = 0;
	while (item[n]) n++;
	
	const char * * newitem = (const char**)malloc(sizeof(char*)*(1+n+1)); // +1 for prefix, +1 for NULL
	
	newitem[0] = prefix;
	for (int i=0;i<=n;i++)
	{
		newitem[1+i] = item[i];
	}
	newitem[1+n] = NULL;
	
	return newitem;
}

//'name' must be suffixed with =
const char * * find_env(const char * * envp, const char * name)
{
	while (*envp)
	{
		if (!memcmp(*envp, name, strlen(name))) return envp;
		envp++;
	}
	return NULL;
}

static inline int execveat(int dirfd, const char * pathname, char * const * argv, char * const * envp, int flags)
{
	if (dirfd != AT_FDCWD) return -ENOSYS;
	if (flags & ~AT_EMPTY_PATH) return -ENOSYS;
	
	int access_ok = access(pathname, X_OK);
	if (access_ok < 0) return access_ok;
	
	//TODO: this leaks lots of memory on failure
	const char * * new_argv = dup_prefix(argv, "[Arlib-sandbox]");
	new_argv[1] = (char*)pathname; // discard our given argv[0], no way to make ld-linux load one file but pass another argv[0]
	
	const char * * new_envp = dup_prefix(envp, NULL);
	const char * * env_pwd = find_env(new_envp+1, "PWD=");
	
	char env_pwd_buf[4+SAND_PATHLEN];
	strcpy(env_pwd_buf, "PWD=");
	strcpy(env_pwd_buf+strlen("PWD="), cwd);
	if (env_pwd)
	{
		*env_pwd = env_pwd_buf;
		new_envp++; // if there is a PWD already, replace it and ignore the prefix slot
	}
	else
	{
		new_envp[0] = env_pwd_buf;
	}
	
	broker_req req = { br_get_emul };
	int fd = do_broker_req(&req);
	if (fd<0) return -ENOMEM;
	
	const char * execveat_gate = (char*)0x00007FFFFFFFEFFF;
	const char * execveat_gate_page = (char*)((long)execveat_gate&~0xFFF);
	intptr_t mmap_ret = (intptr_t)mmap((void*)execveat_gate_page, 4096, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
	if (mmap_ret<0) return mmap_ret;
	
	return syscall5(__NR_execveat, fd, (long)execveat_gate, (long)new_argv, (long)new_envp, AT_EMPTY_PATH);
}

static inline int execve(const char * filename, char * const argv[], char * const envp[])
{
	return execveat(AT_FDCWD, filename, argv, envp, 0);
}

//this one is used to find amount of system RAM, for tuning gcc's garbage collector
//stacktrace: __get_phys_pages(), sysconf(name=_SC_PHYS_PAGES), physmem_total(), init_ggc_heuristics()
//(__get_phys_pages() also needs page size, but that's constant and hardcoded, 4096)
//don't bother giving it the real values, just give it something that looks tasty
static inline int sysinfo_(struct sysinfo * info)
{
	memset(info, 0, sizeof(*info));
	info->uptime = 0;
	info->loads[0] = 0;
	info->loads[1] = 0;
	info->loads[2] = 0;
	info->totalram = 4ULL*1024*1024*1024;
	info->freeram = 4ULL*1024*1024*1024;
	info->sharedram = 0;
	info->bufferram = 0;
	info->totalswap = 0;
	info->freeswap = 0;
	info->procs = 1;
	info->totalhigh = 0;
	info->freehigh = 0;
	info->mem_unit = 1;
	return 0;
}

#define getrusage getrusage_
static inline int getrusage(int who, struct rusage * usage)
{
	memset(usage, 0, sizeof(*usage));
	return 0;
}

#define uname uname_
static inline int uname(struct utsname * buf)
{
	//same as Ubuntu 16.04.0 live CD (despite 16.04 not being able to run the sandbox, CLONE_NEWCGROUP requires kernel 4.6)
	//nodename is considered private information, and child doesn't really need the rest either, just emulate the entire thing
	strcpy(buf->sysname, "Linux");
	strcpy(buf->nodename, "ubuntu");
	strcpy(buf->release, "4.4.0-21-generic");
	strcpy(buf->version, "#37-Ubuntu SMP Mon Apr 18 18:33:37 UTC 2016");
	strcpy(buf->machine, "x86_64");
	strcpy(buf->domainname, "(none)");
	return 0;
}

static inline ssize_t readlink(const char * path, char * buf, size_t bufsiz)
{
	//used by ttyname(3)
	if (!memcmp(path, "/proc/self/fd/", strlen("/proc/self/fd/")))
	{
		return -EACCES;
	}
	error("denied readlink ", path);
	return -EACCES;
}


//errors are returned as -ENOENT, not {errno=ENOENT, -1}; we are (pretend to be) the kernel, not libc
static long syscall_emul(greg_t* regs, int errno)
{
//register assignment per http://stackoverflow.com/a/2538212
#define ARG0 (regs[REG_RAX])
#define ARG1 (regs[REG_RDI])
#define ARG2 (regs[REG_RSI])
#define ARG3 (regs[REG_RDX])
#define ARG4 (regs[REG_R10])
#define ARG5 (regs[REG_R8])
#define ARG6 (regs[REG_R9])
	if (errno != 0)
	{
		error("denied syscall ", ARG0, " (",
		      hex(),ARG1," ", hex(),ARG2," ", hex(),ARG3," ",
		      hex(),ARG4," ", hex(),ARG5," ", hex(),ARG6,")");
		return -errno;
	}
	
#define WRAP0_V(nr, func)                         case nr: return func();
#define WRAP1_V(nr, func, T1)                     case nr: return func((T1)ARG1);
#define WRAP2_V(nr, func, T1, T2)                 case nr: return func((T1)ARG1, (T2)ARG2);
#define WRAP3_V(nr, func, T1, T2, T3)             case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3);
#define WRAP4_V(nr, func, T1, T2, T3, T4)         case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3, (T4)ARG4);
#define WRAP5_V(nr, func, T1, T2, T3, T4, T5)     case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3, (T4)ARG4, (T5)ARG5);
#define WRAP6_V(nr, func, T1, T2, T3, T4, T5, T6) case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3, (T4)ARG4, (T5)ARG5, (T6)ARG6);
#define WRAP0(name)                         WRAP0_V(__NR_##name, name)
#define WRAP1(name, T1)                     WRAP1_V(__NR_##name, name, T1)
#define WRAP2(name, T1, T2)                 WRAP2_V(__NR_##name, name, T1, T2)
#define WRAP3(name, T1, T2, T3)             WRAP3_V(__NR_##name, name, T1, T2, T3)
#define WRAP4(name, T1, T2, T3, T4)         WRAP4_V(__NR_##name, name, T1, T2, T3, T4)
#define WRAP5(name, T1, T2, T3, T4, T5)     WRAP5_V(__NR_##name, name, T1, T2, T3, T4, T5)
#define WRAP6(name, T1, T2, T3, T4, T5, T6) WRAP6_V(__NR_##name, name, T1, T2, T3, T4, T5, T6)
#define WRAP0_(name)                         WRAP0_V(__NR_##name, name##_)
#define WRAP1_(name, T1)                     WRAP1_V(__NR_##name, name##_, T1)
#define WRAP2_(name, T1, T2)                 WRAP2_V(__NR_##name, name##_, T1, T2)
#define WRAP3_(name, T1, T2, T3)             WRAP3_V(__NR_##name, name##_, T1, T2, T3)
#define WRAP4_(name, T1, T2, T3, T4)         WRAP4_V(__NR_##name, name##_, T1, T2, T3, T4)
#define WRAP5_(name, T1, T2, T3, T4, T5)     WRAP5_V(__NR_##name, name##_, T1, T2, T3, T4, T5)
#define WRAP6_(name, T1, T2, T3, T4, T5, T6) WRAP6_V(__NR_##name, name##_, T1, T2, T3, T4, T5, T6)
	switch (ARG0)
	{
	WRAP3(open, char*, int, mode_t);
	WRAP4(openat, int, char*, int, mode_t);
	WRAP2(access, char*, int);
	WRAP2_(stat, char*, struct stat*);
	//treat lstat as stat
	case __NR_lstat: return stat_((char*)ARG1, (struct stat*)ARG2);
	case __NR_fork:  return clone(SIGCHLD, 0, NULL, NULL, 0);
	//vfork manpage:
	//  the behavior is undefined if the process created by
	//  vfork() either modifies any data other than a variable of type pid_t
	//  used to store the return value from vfork(), or returns from the
	//  function in which vfork() was called, or calls any other function
	//  before successfully calling _exit(2) or one of the exec(3) family of
	//  functions
	//this one returns all the way out of a signal handler,
	// and execve() allocates memory and does about a dozen syscalls
	//it would be possible to implement vfork by rewriting the register list, but that wouldn't solve execve
	//instead, it's a lot easier to just implement vfork as normal fork
	//(would've been easier if posix_spawn was a syscall)
	case __NR_vfork: return clone(SIGCHLD, 0, NULL, NULL, 0);
	WRAP3(execve, char*, char**, char**);
	WRAP5(clone, unsigned long, unsigned long, int*, int*, unsigned long);
	WRAP1_(sysinfo, struct sysinfo*);
	WRAP2(getcwd, char*, unsigned long);
	WRAP1(unlink, char*);
	WRAP2(getrusage, int, struct rusage*);
	WRAP2(chmod, char*, mode_t);
	WRAP3(readlink, char*, char*, size_t);
	WRAP1(chdir, char*);
	WRAP1(uname, struct utsname*);
	default:
		error("can't emulate syscall ", ARG0);
		return -ENOSYS;
	}
#undef WRAP0_V
#undef WRAP1_V
#undef WRAP2_V
#undef WRAP3_V
#undef WRAP4_V
#undef WRAP5_V
#undef WRAP6_V
#undef WRAP0
#undef WRAP1
#undef WRAP2
#undef WRAP3
#undef WRAP4
#undef WRAP5
#undef WRAP6
#undef WRAP0_
#undef WRAP1_
#undef WRAP2_
#undef WRAP3_
#undef WRAP4_
#undef WRAP5_
#undef WRAP6_
#undef ARG0
#undef ARG1
#undef ARG2
#undef ARG3
#undef ARG4
#undef ARG5
#undef ARG6
}



#define sigaction kabi_sigaction
#undef sa_handler
#undef sa_sigaction
#define SA_RESTORER 0x04000000
struct kabi_sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t*, void*);
	};
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	char sa_mask[_NSIG/8]; // this isn't the correct declaration, but we don't use this field, we only need its size
};

static inline int rt_sigaction(int sig, const struct sigaction * act, struct sigaction * oact, size_t sigsetsize)
{
	return syscall4(__NR_rt_sigaction, sig, (long)act, (long)oact, sigsetsize);
}

static inline int rt_sigprocmask(int how, const sigset_t* nset, sigset_t* oset, size_t sigsetsize)
{
	return syscall4(__NR_rt_sigprocmask, how, (long)nset, (long)oset, sigsetsize);
}

static inline void set_sighand(int sig, void(*handler)(int, siginfo_t*, void*), int flags)
{
	struct sigaction act;
	act.sa_sigaction = handler;
	//sa_restorer is mandatory; judging by kernel source, this is to allow nonexecutable stack
	//(should've put it in VDSO, but I guess this syscall is older than VDSO)
	act.sa_flags = SA_SIGINFO | SA_RESTORER | flags;
	//and for some reason, I get runtime relocations if I try accessing it from C++, so let's switch language
	__asm__("lea %0, [%%rip+restore_rt]" : "=r"(act.sa_restorer));
	memset(&act.sa_mask, 0, sizeof(act.sa_mask));
	rt_sigaction(sig, &act, NULL, sizeof(act.sa_mask));
}
#define STR_(x) #x
#define STR(x) STR_(x)
__asm__(R"(
#sigaction.sa_restorer takes its arguments from the stack, have to implement it in assembly
#otherwise GCC could do something stupid, like set up a frame pointer
restore_rt:
mov %eax, )" STR(__NR_rt_sigreturn) R"(
syscall
)");

static void sa_sigsys(int signo, siginfo_t* info, void* context)
{
	ucontext_t* uctx = (ucontext_t*)context;
	long ret = syscall_emul(uctx->uc_mcontext.gregs, info->si_errno);
	uctx->uc_mcontext.gregs[REG_RAX] = ret;
}

extern "C" void preload_action(char** argv, char** envp)
{
	//what's this flag for?
	//answer: kernel usually blocks the currently-executing signal, to avoid infinite recursion
	// (for example, if the SIGSEGV handler segfaults, running it again probably won't help)
	//a good idea most of the time, but not for us; we know this one doesn't recurse,
	// and we don't always return from this signal handler, sometimes we execveat() instead
	//so we pass SA_NODEFER to disable this self-blocking
	set_sighand(SIGSYS, sa_sigsys, SA_NODEFER); 
	
	progname = argv[1];
	
	const char * * env_pwd = find_env((const char**)envp, "PWD=");
	if (env_pwd) chdir(*env_pwd + strlen("PWD="));
	else chdir("/@CWD");
}

extern "C" void preload_error(const char * why)
{
	error(why);
	exit_group(1);
}

}}
#endif
