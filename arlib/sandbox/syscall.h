#pragma once
//usage: int fd = syscall2(__NR_open, "foo", O_RDONLY);
//WARNING: uses the raw kernel interface!
//If the manpage splits an argument in high/low, then you'd better follow suit.
//If the argument order changes between platforms, you must follow that.
//If the syscall is completely different from the wrapper (hi clone()), you must use syscall semantics.
//In particular, there is no errno in this environment. Instead, that's handled by returning -ENOENT.

#ifdef __x86_64__
#define CLOBBER "memory", "cc", "rcx", "r11" // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#syscall

static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	//register assignment per http://stackoverflow.com/a/2538212
	register long rax asm("rax") = n;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	register long r10 asm("r10") = a4;
	register long r8 asm("r8") = a5;
	register long r9 asm("r9") = a6;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax), "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9) : CLOBBER);
	return rax;
}

static inline long syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
	register long rax asm("rax") = n;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	register long r10 asm("r10") = a4;
	register long r8 asm("r8") = a5;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax), "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8) : CLOBBER);
	return rax;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4)
{
	register long rax asm("rax") = n;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	register long r10 asm("r10") = a4;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax), "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10) : CLOBBER);
	return rax;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
	register long rax asm("rax") = n;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	register long rdx asm("rdx") = a3;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax), "r"(rdi), "r"(rsi), "r"(rdx) : CLOBBER);
	return rax;
}

static inline long syscall2(long n, long a1, long a2)
{
	register long rax asm("rax") = n;
	register long rdi asm("rdi") = a1;
	register long rsi asm("rsi") = a2;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax), "r"(rdi), "r"(rsi) : CLOBBER);
	return rax;
}

static inline long syscall1(long n, long a1)
{
	register long rax asm("rax") = n;
	register long rdi asm("rdi") = a1;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax), "r"(rdi) : CLOBBER);
	return rax;
}

static inline long syscall0(long n)
{
	register long rax asm("rax") = n;
	__asm__ volatile("syscall" : "=r"(rax) : "r"(rax) : CLOBBER);
	return rax;
}
#endif
