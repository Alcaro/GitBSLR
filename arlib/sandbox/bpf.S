let arch = (offsetof(struct seccomp_data, arch))
let sysno = (offsetof(struct seccomp_data, nr))
let arg1l = (offsetof(struct seccomp_data, args[0]))
let arg1h = (offsetof(struct seccomp_data, args[0])+4)
let arg2l = (offsetof(struct seccomp_data, args[1]))
let arg2h = (offsetof(struct seccomp_data, args[1])+4)
let arg3l = (offsetof(struct seccomp_data, args[2]))
let arg3h = (offsetof(struct seccomp_data, args[2])+4)
let arg4l = (offsetof(struct seccomp_data, args[3]))
let arg4h = (offsetof(struct seccomp_data, args[3])+4)
let arg5l = (offsetof(struct seccomp_data, args[4]))
let arg5h = (offsetof(struct seccomp_data, args[4])+4)
let arg6l = (offsetof(struct seccomp_data, args[5]))
let arg6h = (offsetof(struct seccomp_data, args[5])+4)

#some things are passed to the compiler, like offsetof
#but some things are handled internally, so gcc gets fewer include files to parse, and to allow this jump ladder to become a tree
defines /usr/include/x86_64-linux-gnu/asm/unistd_64.h

ld [arch]
jne #AUDIT_ARCH_X86_64, ENOSYS

ld [sysno]
jeq #__NR_open, emulate
jeq #__NR_openat, emulate
jeq #__NR_stat, emulate
jeq #__NR_lstat, emulate
jeq #__NR_access, emulate
jeq #__NR_unlink, emulate
jeq #__NR_chmod, emulate
jeq #__NR_umask, accept
jeq #__NR_readlink, emulate

jeq #__NR_read, accept
jeq #__NR_readv, accept
jeq #__NR_write, accept
jeq #__NR_writev, accept
jeq #__NR_close, accept
jeq #__NR_lseek, accept
jeq #__NR_fstat, accept
jeq #__NR_fcntl, fcntl
jeq #__NR_fchmod, accept

jeq #__NR_ioctl, ioctl
jeq #__NR_getcwd, emulate
jeq #__NR_chdir, emulate
jeq #__NR_getdents, accept

jeq #__NR_dup, accept
jeq #__NR_dup2, accept
jeq #__NR_pipe, accept

jeq #__NR_brk, accept
jeq #__NR_mmap, accept
jeq #__NR_mremap, accept
jeq #__NR_mprotect, accept
jeq #__NR_munmap, accept

jeq #__NR_arch_prctl, arch_prctl
jeq #__NR_exit, accept
jeq #__NR_exit_group, accept
jeq #__NR_set_tid_address, accept
jeq #__NR_set_robust_list, accept
jeq #__NR_prlimit64, prlimit64

jeq #__NR_fork, emulate
jeq #__NR_vfork, emulate
jeq #__NR_clone, clone
jeq #__NR_wait4, wait4
jeq #__NR_futex, futex

jeq #__NR_execve, emulate
jeq #__NR_execveat, execveat

#PID is private, but it's in a PID namespace so it won't get the real one
jeq #__NR_getpid, accept
jeq #__NR_getppid, accept
jeq #__NR_getuid, accept
jeq #__NR_getgid, accept
jeq #__NR_geteuid, accept
jeq #__NR_getegid, accept

jeq #__NR_getrlimit, accept
#setrlimit may seem harmful, but it can't raise the hard limits so it's not
jeq #__NR_setrlimit, accept
jeq #__NR_sysinfo, emulate
jeq #__NR_getrusage, emulate
jeq #__NR_uname, emulate

jeq #__NR_rt_sigaction, accept
jeq #__NR_rt_sigreturn, accept
jeq #__NR_rt_sigprocmask, accept
jeq #__NR_sendto, sendto
jeq #__NR_recvmsg, accept

#these are in vdso, I can't block them even if I want to
jeq #__NR_clock_gettime, accept
jeq #__NR_getcpu, accept
jeq #__NR_gettimeofday, accept
jeq #__NR_time, accept

jmp ENOSYS


ioctl:
#int ioctl(int fd, int cmd, ...)
defines /usr/include/asm-generic/ioctls.h
ld [arg2h]
jne #0, ENOTTY_S
ld [arg2l]
#TODO: move these to broker policy
jeq #TCGETS, accept
jeq #TIOCGWINSZ, accept

jmp ENOTTY_S


fcntl:
#int fcntl(int fd, int cmd, ... /* arg */)
ld [arg2h]
jne #0, EINVAL
ld [arg2l]

#sets only O_CLOEXEC
jeq #F_GETFD, accept
jeq #F_SETFD, accept

#can't change writability flags, so harmless
jeq #F_GETFL, accept
jeq #F_SETFL, accept

#the dup() syscall itself is fine, so this is too
jeq #F_DUPFD, accept
jeq #F_DUPFD_CLOEXEC, accept

jmp EINVAL


execveat:
#int execveat(int dirfd, const char * pathname, char * const * argv, char * const * envp, int flags)
#accept from sysemu, intercept from others
#sysemu is detected by pathname == 0x00007fff'ffffefff (the last mappable byte)

ld [arg2h]
jne #0x00007fff, emulate
ld [arg2l]
jne #0xffffefff, emulate
jmp accept


prlimit64:
#most of this syscall is safe, everything I want to enforce is a hard limit and prlimit64 can't touch that
#but block it anyways because why not
ld [arg2h]
jne #0, ENOSYS
ld [arg2l]
jeq #RLIMIT_STACK, accept
jmp ENOSYS


clone:
#pid_t clone(unsigned long clone_flags, unsigned long newsp, int* parent_tidptr, int* child_tidptr, unsigned long tls)
#accept from sysemu, intercept from others
#sysemu is detected by flags&CLONE_CHILD_SETTID && child_tidptr==NULL

ld [arg1h]
jne #0, EINVAL
ld [arg1l]
#threads are fine, there's not even any point intercepting them
jset #CLONE_THREAD, clone_thread

#for fork(), only this flag is allowed, no unsharing
jne #SIGCHLD|CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID|CLONE_PARENT_SETTID, emulate
ld [arg3h]
jne #0, emulate
ld [arg3l]
jne #0, emulate
jmp accept

clone_thread:
#however, we must reject any attempt to half-share stuff between threads; either share everything or nothing
#some of these are harmless, but let's reject them all anyways, for simplicity
#broken apart to reduce line length
let clone_share_fl = CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_SYSVSEM
let clone_set_fl = CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID
jne #CLONE_THREAD|clone_share_fl|clone_set_fl, EINVAL
#TODO: ensure this is safe
jmp EINVAL


arch_prctl:
#int arch_prctl(int code, unsigned long addr)
#int arch_prctl(int code, unsigned long * addr)
#ensure code is one of the four documented values, nothing funny
defines /usr/include/x86_64-linux-gnu/asm/prctl.h
ld [arg1h]
jne #0, ENOSYS
ld [arg1l]
jeq #ARCH_SET_FS, accept
jeq #ARCH_GET_FS, accept
jeq #ARCH_SET_GS, accept
jeq #ARCH_GET_GS, accept
jmp ENOSYS


sendto:
#ssize_t sendto(int sockfd, const void * buf, size_t len, int flags,
#               const struct sockaddr * dest_addr, socklen_t addrlen)
#there is no send syscall, it's implemented as sendto(dest_addr=NULL, addrlen=0)
#ensure this isn't a sendto trying to sneak around
ld [arg5h]
jne #0, EINVAL
ld [arg5l]
jne #0, EINVAL
ld [arg6h]
jne #0, EINVAL
ld [arg6l]
jne #0, EINVAL
jmp accept


wait4:
#pid_t wait4(pid_t pid, int * status, int options, struct rusage * rusage)
#getrusage is blocked, so rusage!=NULL must be blocked too
ld [arg4h]
jne #0, EINVAL
ld [arg4l]
jne #0, EINVAL
jmp accept


futex:
#int futex(int* uaddr, int futex_op, int val, const struct timespec * timeout, int * uaddr2, int val3)
#int futex(int* uaddr, int futex_op, int val, uint32_t val2, int * uaddr2, int val3)
defines /usr/include/linux/futex.h
ld [arg2h]
jne #0, EINVAL
ld [arg2l]
and #~(FUTEX_PRIVATE_FLAG|FUTEX_CLOCK_REALTIME)
jeq #FUTEX_WAIT, accept
jeq #FUTEX_WAKE, accept
#reject FD
jeq #FUTEX_REQUEUE, accept
jeq #FUTEX_CMP_REQUEUE, accept
jeq #FUTEX_WAKE_OP, accept
#reject {,UN,TRY}LOCK_PI
jeq #FUTEX_WAIT_BITSET, accept
jeq #FUTEX_WAKE_BITSET, accept
#reject {WAIT,CMD}_REQUEUE_PI
jmp ENOSYS


#accept is also a syscall, but there's no need to parse its arguments (or accept it at all), so I'll reuse its name
accept:
ret #SECCOMP_RET_ALLOW

emulate:
ret #SECCOMP_RET_TRAP|0

ENOSYS:
#let sysemu whine, then return ENOSYS
ret #SECCOMP_RET_TRAP|ENOSYS

EINVAL:
ret #SECCOMP_RET_TRAP|EINVAL

EACCES:
ret #SECCOMP_RET_TRAP|EACCES

ENOSYS_S:
#return ENOSYS without sysemu whining
ret #SECCOMP_RET_ERRNO|ENOSYS

ENOTTY_S:
ret #SECCOMP_RET_ERRNO|ENOTTY
