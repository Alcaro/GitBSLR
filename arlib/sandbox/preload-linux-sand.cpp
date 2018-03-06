#ifdef SANDBOX_INTERNAL

//These 200 lines of code implement one tiny thing: Call one function (sysemu.cpp), then pass control to ld-linux.so.

//The sandbox environment is quite strict. It can't run normal programs unchanged, they'd call open() which fails.
//LD_PRELOAD doesn't help either, the loader open()s all libraries before initializing that (and init order isn't guaranteed).
//Its lesser known cousin LD_AUDIT works better, but the library itself is open()ed.
//This can be worked around with ptrace to make that specific syscall return a pre-opened fd, but that's way too much effort.
//Instead, we will be the loader. Of course we don't want to actually load the program, that's even harder than ptrace,
// so we'll just load the real loader, after setting up a SIGSYS handler (see sysemu.cpp) to let open() act normally.
//We won't do much error checking. We know everything will succeed, and if it doesn't, there's no way to recover.

//#define _GNU_SOURCE // default (mandatory) in c++
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <elf.h>

#include "syscall.h"

//gcc recognizes various function names and reads attributes (such as extern) from the headers, force it not to
namespace mysand { namespace {

//have to redefine the entire libc, fun
//(there's some copying between this and sysemu.cpp)
static inline void memset(void* ptr, int value, size_t num)
{
	//compiler probably optimizes this
	uint8_t* ptr_ = (uint8_t*)ptr;
	for (size_t i=0;i<num;i++) ptr_[i] = value;
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

static inline int open(const char * pathname, int flags, mode_t mode = 0)
{
	return syscall3(__NR_open, (long)pathname, flags, mode);
}

static inline ssize_t read(int fd, void * buf, size_t count)
{
	return syscall3(__NR_read, fd, (long)buf, count);
}

static inline int close(int fd)
{
	return syscall1(__NR_close, fd);
}

static inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void*)syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

static inline int munmap(void* addr, size_t length)
{
	return syscall2(__NR_munmap, (long)addr, length);
}

#define ALIGN 4096
#define ALIGN_MASK (ALIGN-1)
#define ALIGN_OFF(ptr) ((uintptr_t)(ptr) & ALIGN_MASK)
#define ALIGN_DOWN(ptr) ((ptr) - ALIGN_OFF(ptr))
#define ALIGN_UP(ptr) (ALIGN_DOWN(ptr) + (ALIGN_OFF(ptr) ? ALIGN : 0))

static inline Elf64_Ehdr* map_binary(int fd, uint8_t*& base, uint8_t* hbuf, size_t buflen)
{
	//uselib() would be the easy way out, but it doesn't tell where it's mapped, and it may be compiled out of the kernel
	read(fd, hbuf, buflen);
	
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)hbuf;
	static const unsigned char exp_hdr[16] = { '\x7F', 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV, 0 };
	if (memcmp(ehdr->e_ident, exp_hdr, 16) != 0) return NULL;
	if (ehdr->e_type != ET_DYN) return NULL;
	
	Elf64_Phdr* phdr = (Elf64_Phdr*)(hbuf + ehdr->e_phoff);
	
	//find how big it needs to be
	size_t memsize = 0;
	for (int i=0;i<ehdr->e_phnum;i++)
	{
		if (phdr[i].p_type != PT_LOAD) continue;
		
		size_t thissize = ALIGN_UP(phdr[i].p_vaddr + phdr[i].p_memsz);
		if (thissize > memsize) memsize = thissize;
	}
	
	//find somewhere it fits (this unfortunately under-aligns it - could fix it if needed, but it seems to work in practice)
	base = (uint8_t*)mmap(NULL, memsize, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
	if (base == MAP_FAILED) return NULL;
	munmap(base, memsize); // in a multithreaded process, this would be a race condition, but we're single threaded
	                       // we can map on top of the target, but that leaves pointless PROT_NONE holes
	                       // they can be removed manually, but this is easier
	
	for (int i=0;i<ehdr->e_phnum;i++)
	{
		//not sure if other segment types are needed, but kernel doesn't use them
		if (phdr[i].p_type != PT_LOAD) continue;
		
		int prot = 0;
		if (phdr[i].p_flags & PF_R) prot |= PROT_READ;
		if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
		if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;
		
		uint8_t* map_start = ALIGN_DOWN(base + phdr[i].p_vaddr);
		uint8_t* map_end = ALIGN_UP(base + phdr[i].p_vaddr + phdr[i].p_memsz);
		
		if (mmap(map_start, map_end-map_start, prot, MAP_PRIVATE|MAP_FIXED, fd, ALIGN_DOWN(phdr[i].p_offset)) != map_start)
		{
			return NULL;
		}
		if (phdr[i].p_memsz != phdr[i].p_filesz)
		{
			//no clue why this ALIGN_UP is needed, but it segfaults without it. is p_memsz wrong and nobody noticed?
			uint8_t* clear_start = base + phdr[i].p_vaddr + phdr[i].p_filesz;
			uint8_t* clear_end = ALIGN_UP(base + phdr[i].p_vaddr + phdr[i].p_memsz);
			memset(clear_start, 0, clear_end-clear_start);
		}
	}
	
	return ehdr;
}

//ld-linux can be the main program, in which case it opens the main binary as a normal library and executes that.
//It checks this by checking if the main program's entry point is its own.
//So how does the loader find this entry point? As we all know, main() has three arguments: argc,
// argv, envp. But the loader actually gets a fourth argument, auxv, containing some entropy (for
// stack cookies), user ID, page size, ELF header size, the main program's entry point, and some
// other stuff.
//Since we're the main program, we're the entry point, both in auxv and the actual entry, and
// ld-linux won't recognize us. But we know where ld-linux starts, so let's put it in auxv. It's on
// the stack, it's writable. (ld-linux replaces a few auxv entries too, it's only fair.)
typedef void(*funcptr)();
static inline void update_auxv(Elf64_auxv_t* auxv, uint8_t* base, Elf64_Ehdr* ehdr)
{
	for (int i=0;auxv[i].a_type != AT_NULL;i++)
	{
		//don't think ld-linux uses PHDR or PHNUM, but why not
		if (auxv[i].a_type == AT_PHDR)
		{
			auxv[i].a_un.a_val = (uintptr_t)base + ehdr->e_phoff;
		}
		if (auxv[i].a_type == AT_PHNUM)
		{
			auxv[i].a_un.a_val = ehdr->e_phnum;
		}
		//AT_BASE looks like it should be relevant, but is actually NULL
		if (auxv[i].a_type == AT_ENTRY)
		{
			//a_un is a union, but it only has one member. Apparently the rest were removed to allow
			// a 64bit program to use the 32bit structs. Even though auxv isn't accessible on wrong
			// bitness. I guess someone removed all pointers, sensible or not.
			//Backwards compatibility, fun for everyone...
			auxv[i].a_un.a_val = (uintptr_t)(funcptr)(base + ehdr->e_entry);
		}
	}
}


extern "C" void preload_action(char** argv, char** envp);
extern "C" void preload_error(const char * why); // doesn't return
extern "C" funcptr bootstrap_start(void** stack)
{
	int* argc = (int*)stack;
	char* * argv = (char**)(stack+1);
	char* * envp = argv+*argc+1;
	void* * tmp = (void**)envp;
	while (*tmp) tmp++;
	Elf64_auxv_t* auxv = (Elf64_auxv_t*)(tmp+1);
	
	preload_action(argv, envp);
	
	//this could call the syscall emulator directly, but if I don't, the preloader can run unsandboxed as well
	//it means a SIGSYS penalty, but there's dozens of those already, another one makes no difference
	int fd = open("/lib64/ld-linux-x86-64.so.2", O_RDONLY);
	if (fd < 0) preload_error("couldn't open dynamic linker");
	uint8_t hbuf[832]; // FILEBUF_SIZE from glibc elf/dl-load.c
	uint8_t* ld_base;  // ld-linux has 7 segments, 832 bytes fits 13 (plus ELF header)
	ld_base = NULL; // shut up false positive warning
	Elf64_Ehdr* ld_ehdr = map_binary(fd, ld_base, hbuf, sizeof(hbuf));
	close(fd);
	if (!ld_ehdr) preload_error("couldn't initialize dynamic linker");
	
	update_auxv(auxv, ld_base, ld_ehdr);
	
	return (funcptr)(ld_base + ld_ehdr->e_entry);
}

}}

//we need the stack pointer
//I could take the address of an argument, but I'm pretty sure that's undefined behavior
//and our callee also needs the stack, and I don't trust gcc to enforce a tail call
//therefore, the bootloader's first step must be written in asssembly
__asm__(R"(
# asm works great with raw strings, surprised I haven't seen it elsewhere
# guess c++ and asm is a rare combination
.globl _start
_start:
mov %rdi, %rsp
push %rcx  # nothing in bootstrap_start seems to require stack alignment, but better do it anyways
call bootstrap_start
pop %rcx  # why rcx? no real reason, I just picked one at random
jmp %rax
)");


#else
#define STR_(x) #x
#define STR(x) STR_(x)

extern const char sandbox_preload_bin[];
extern const unsigned sandbox_preload_len;
__asm__(R"(
.section .rodata
.global sandbox_preload_bin
sandbox_preload_bin:
.incbin "obj/sand-preload-)" STR(ARLIB_OBJNAME) R"(.elf"
.equ my_len, .-sandbox_preload_bin
.align 4
.global sandbox_preload_len
sandbox_preload_len:
.int my_len
.section .text
)");
#endif
