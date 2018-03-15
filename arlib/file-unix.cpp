#include "file.h"
#include "os.h"
#include "thread.h"
#include "init.h"

#ifdef __unix__
#include <unistd.h>
//#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

//static void window_cwd_enter(const char * dir);
//static void window_cwd_leave();
//
//char * _window_native_get_absolute_path(const char * basepath, const char * path, bool allow_up)
//{
//	if (!basepath || !path) return NULL;
//	const char * filepart=strrchr(basepath, '/');
//	if (!filepart) return NULL;
//	char * basedir=strndup(basepath, filepart+1-basepath);
//	
//	window_cwd_enter(basedir);
//	char * ret=realpath(path, NULL);
//	window_cwd_leave();
//	
//	if (!allow_up && ret && strncasecmp(basedir, ret, filepart+1-basepath)!=0)
//	{
//		free(ret);
//		ret=NULL;
//	}
//	free(basedir);
//	
//	return ret;
//}
//
//static const char * cwd_init;
//static const char * cwd_bogus;
//static mutex cwd_mutex;
//
//static void window_cwd_enter(const char * dir)
//{
//	cwd_mutex.lock();
//	char * cwd_bogus_check=getcwd(NULL, 0);
//	if (strcmp(cwd_bogus, cwd_bogus_check)!=0) abort();//if this fires, someone changed the directory without us knowing - not allowed. cwd belongs to the frontend.
//	free(cwd_bogus_check);
//	ignore(chdir(dir));
//}
//
//static void window_cwd_leave()
//{
//	ignore(chdir(cwd_bogus));
//	cwd_mutex.unlock();
//}
//
//const char * window_get_cwd()
//{
//	return cwd_init;
//}
//
//void _window_init_file()
//{
//	char * cwd_init_tmp=getcwd(NULL, 0);
//	char * cwdend=strrchr(cwd_init_tmp, '/');
//	if (!cwdend) cwd_init="/";
//	else if (cwdend[1]=='/') cwd_init=cwd_init_tmp;
//	else
//	{
//		size_t cwdlen=strlen(cwd_init_tmp);
//		char * cwd_init_fixed=malloc(cwdlen+1+1);
//		memcpy(cwd_init_fixed, cwd_init_tmp, cwdlen);
//		cwd_init_fixed[cwdlen+0]='/';
//		cwd_init_fixed[cwdlen+1]='\0';
//		cwd_init=cwd_init_fixed;
//		free(cwd_init_tmp);
//	}
//	
//	//try a couple of useless directories and hope one of them works
//	//this seems to be the best one:
//	//- even root can't create files here
//	//- it contains no files with a plausible name on a standard Ubuntu box (I have an ath9k-phy0, nothing will ever want that filename)
//	//- a wild write will not do anything dangerous except turn on some lamps
//	!chdir("/sys/class/leds/") ||
//		//the rest are in case it's not accessible (weird chroot? not linux?), so try some random things
//		!chdir("/sys/") ||
//		!chdir("/dev/") ||
//		!chdir("/home/") ||
//		!chdir("/tmp/") ||
//		!chdir("/");
//	cwd_bogus = getcwd(NULL, 0);//POSIX does not specify getcwd(NULL), it's Linux-specific
//}


#if defined(__x86_64__) || defined(__i386__)
static const long pagesize = 4096;
#else
static const long pagesize = sysconf(_SC_PAGESIZE);
#endif

namespace {
	class file_unix : public file::impl {
	public:
		int fd;
		
		file_unix(int fd) : fd(fd) {}
		
		size_t size()
		{
			return lseek(fd, 0, SEEK_END);
		}
		bool resize(size_t newsize)
		{
			return (ftruncate(this->fd, newsize) == 0);
		}
		
		size_t pread(arrayvieww<byte> target, size_t start)
		{
			size_t ret = ::pread(fd, target.ptr(), target.size(), start);
			if (ret<0) return 0;
			else return ret;
		}
		bool pwrite(arrayview<byte> data, size_t start)
		{
			size_t ret = ::pwrite(fd, data.ptr(), data.size(), start);
			if (ret<0) return 0;
			else return ret;
		}
		
		/*private*/ arrayvieww<byte> mmap(bool writable, size_t start, size_t len)
		{
			//TODO: for small things (64KB? 1MB?), use malloc, it's faster
			//http://lkml.iu.edu/hypermail/linux/kernel/0004.0/0728.html
			size_t offset = start % pagesize;
			void* data = ::mmap(NULL, len+offset, writable ? PROT_WRITE|PROT_READ : PROT_READ, MAP_SHARED, this->fd, start-offset);
			if (data == MAP_FAILED) return NULL;
			return arrayvieww<byte>((uint8_t*)data+offset, len);
		}
		
		arrayview<byte> mmap(size_t start, size_t len) { return mmap(false, start, len); }
		void unmap(arrayview<byte> data)
		{
			size_t offset = (uintptr_t)data.ptr() % pagesize;
			munmap((char*)data.ptr()-offset, data.size()+offset);
		}
		
		arrayvieww<byte> mmapw(size_t start, size_t len) { return mmap(true, start, len); }
		bool unmapw(arrayvieww<byte> data)
		{
			unmap(data);
			// manpage documents no errors for the case where file writing fails, gotta assume it never does
			return true;
		}
		
		~file_unix() { close(fd); }
	};
}

file::impl* file::open_impl_fs(cstring filename, mode m)
{
	static const int flags[] = { O_RDONLY, O_RDWR|O_CREAT, O_RDWR, O_RDWR|O_CREAT|O_TRUNC, O_RDWR|O_CREAT|O_EXCL };
	int fd = ::open(filename.c_str(), flags[m]|O_CLOEXEC, 0666);
	if (fd<0) return NULL;
	
	struct stat st;
	fstat(fd, &st);
	if (!S_ISREG(st.st_mode)) // no opening directories
	{
		::close(fd);
		return NULL;
	}
	
	return new file_unix(fd);
}

bool file::unlink_fs(cstring filename)
{
	int ret = ::unlink(filename.c_str());
	return ret==0 || (ret==-1 && errno==ENOENT);
}

string file::dirname(cstring path)
{
	return path.rsplit<1>("/")[0]+"/";
}
string file::basename(cstring path)
{
	return path.rsplit<1>("/")[1];
}

#ifdef ARGUI_NONE
file::impl* file::open_impl(cstring filename, mode m)
{
	return open_impl_fs(filename, m);
}

bool file::unlink(cstring filename)
{
	return unlink_fs(filename);
}
#endif


static string exepath;
cstring file::exepath() { return ::exepath; }
static string cwd;
cstring file::cwd() { return ::cwd; }

void arlib_init_file()
{
	array<char> buf;
	buf.resize(64);
	
again: ;
	ssize_t r = readlink("/proc/self/exe", buf.ptr(), buf.size());
	if (r <= 0) abort();
	if ((size_t)r >= buf.size()-1)
	{
		buf.resize(buf.size() * 2);
		goto again;
	}
	
	buf[r] = '\0';
	char * end = strrchr(buf.ptr(), '/')+1; // a / is known to exist
	*end = '\0';
	
	exepath = buf.ptr();
	
	
	cwd = getcwd(NULL, 0);
	if (!cwd.endswith("/")) cwd += "/";
	
	//char * cwd_init_tmp=getcwd(NULL, 0);
	//char * cwdend=strrchr(cwd_init_tmp, '/');
	//if (!cwdend) cwd_init="/";
	//else if (cwdend[1]=='/') cwd_init=cwd_init_tmp;
	//else
	//{
	//	size_t cwdlen=strlen(cwd_init_tmp);
	//	char * cwd_init_fixed=malloc(cwdlen+1+1);
	//	memcpy(cwd_init_fixed, cwd_init_tmp, cwdlen);
	//	cwd_init_fixed[cwdlen+0]='/';
	//	cwd_init_fixed[cwdlen+1]='\0';
	//	cwd_init=cwd_init_fixed;
	//	free(cwd_init_tmp);
	//}
	
//	//disable cwd
//	//try a couple of useless directories and hope one of them works
//	//this seems to be the best one:
//	//- even root can't create files here
//	//- it contains no files with a plausible name on a standard Ubuntu box
//	//    (I have an ath9k-phy0, and a bunch of stuff with colons, nothing will ever want those filenames)
//	//- a wild write will not do anything dangerous except turn on some lamps
//	!chdir("/sys/class/leds/") ||
//		//the rest are in case it's not accessible (weird chroot? not linux?), so try some random things
//		!chdir("/sys/") ||
//		!chdir("/dev/") ||
//		!chdir("/home/") ||
//		!chdir("/tmp/") ||
//		!chdir("/");
//	cwd_bogus = getcwd(NULL, 0); // POSIX does not specify getcwd(NULL), it's Linux-specific
}
#endif
