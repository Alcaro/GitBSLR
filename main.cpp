//GitBSLR - make Git follow symlinks
//More specifically:
//- Symlinks to anywhere inside the repo are still symlinks.
//- Relative symlinks to outside the repository are inlined.
//- Absolute symlinks are left alone, though they're not recommended.
//- If following symlinks yields a loop (for example a link to the parent of the repo), the loop point is turned into a symlink.
//- If multiple symlinks lead to outside the repo, you'll probably end up with duplicate files.
//- Interaction with Git's cross-filesystem detector is untested.
//- Submodules are untested.
//- Unix only (only Linux tested), no Windows support (but symlinks barely work on Windows in the first place).

#include "arlib.h"

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int (*chdir_t)(const char * path);
typedef ssize_t (*readlink_t)(const char * path, char * buf, size_t bufsiz);
typedef struct dirent* (*readdir_t)(DIR* dirp);
typedef struct dirent64* (*readdir64_t)(DIR* dirp);

static chdir_t chdir_o;
static readlink_t readlink_o;
static readdir_t readdir_o;
static readdir64_t readdir64_o;


static string readlink_d(cstring path)
{
	array<char> buf;
	buf.resize(64);
	
again: ;
	ssize_t r = readlink_o(path.c_str(), buf.ptr(), buf.size());
	if (r <= 0) return "";
	if ((size_t)r >= buf.size()-1)
	{
		buf.resize(buf.size() * 2);
		goto again;
	}
	
	buf[r] = '\0';
	return buf.ptr();
}

//Input:
// A path to a file, relative to the Git repo root, usable with stat() or readlink().
//Output:
// If that path should refer to a symlink, return what it points to (relative to the presumed link's parent directory).
// If it doesn't exist, or shouldn't be a symlink, return a blank string.
//The function may not call lstat or readlink, that'd yield infinite recursion. Instead, append _o and call that.

//algorithm:
//for each prefix of the path:
// if path is the same thing as prefix:
//  it's a link
// if path is a link, and points to inside prefix:
//  it's a link
//otherwise, it's not a link
static string resolve_symlink(cstring path)
{
	string path_linktarget = readlink_d(path);
	if (path_linktarget[0] == '/') return path_linktarget;
	
	string path_abs = string::create_usurp(realpath(path.c_str(), NULL));
	
	struct stat64 st_target;
	__xstat64(_STAT_VER, path.c_str(), &st_target);
	
	array<cstring> parts = path.csplit("/");
	for (size_t i=0;i<parts.size();i++)
	{
		string newpath = parts.slice(0,i).join("/");
		if (newpath == "") newpath = ".";
		string newpath_abs = string::create_usurp(realpath(newpath, NULL));
		
		struct stat64 st;
		__xstat64(_STAT_VER, newpath, &st);
		
		if (st_target.st_dev == st.st_dev && st_target.st_ino == st.st_ino)
		{
			if (i == parts.size()-1) return ".";
			string ret;
			for (size_t j=i;j<parts.size()-1;j++) ret += "../";
			return ret.substr(0, ~1);
		}
		
		if (path_linktarget && path_abs.startswith(newpath_abs+"/"))
		{
			return path_linktarget;
		}
	}
	return "";
}




static bool initialized = false;

__attribute__((constructor)) static void init()
{
	chdir_o = (chdir_t)dlsym(RTLD_NEXT, "chdir");
	readlink_o = (readlink_t)dlsym(RTLD_NEXT, "readlink");
	readdir_o = (readdir_t)dlsym(RTLD_NEXT, "readdir");
	readdir64_o = (readdir64_t)dlsym(RTLD_NEXT, "readdir64");
}


//the first thing Git does is find .git and chdir to it
//.. and .git are never symlinks and should never be, so before chdir is called, 
DLLEXPORT int chdir(const char * path);
DLLEXPORT int chdir(const char * path)
{
	initialized = true;
	return chdir_o(path);
}

DLLEXPORT int lstat(const char * path, struct stat* buf);
DLLEXPORT int lstat(const char * path, struct stat* buf)
{
	int ret = stat(path, buf);
	if (ret<0 || !initialized) return ret;
	
	string newpath = resolve_symlink(path);
	if (newpath)
	{
		buf->st_mode &= ~S_IFMT;
		buf->st_mode |= S_IFLNK;
		buf->st_size = newpath.length();
	}
	return ret;
}

DLLEXPORT int __lxstat64(int ver, const char * path, struct stat64* buf);
DLLEXPORT int __lxstat64(int ver, const char * path, struct stat64* buf)
{
	int ret = __xstat64(ver, path, buf);
	if (ret<0 || !initialized) return ret;
	
	string newpath = resolve_symlink(path);
	if (newpath)
	{
		buf->st_mode &= ~S_IFMT;
		buf->st_mode |= S_IFLNK;
		buf->st_size = newpath.length();
	}
	return ret;
}

DLLEXPORT ssize_t readlink(const char * path, char * buf, size_t bufsiz);
DLLEXPORT ssize_t readlink(const char * path, char * buf, size_t bufsiz)
{
	if (!initialized) return readlink_o(path, buf, bufsiz);
	
	string newpath = resolve_symlink(path);
	if (!newpath)
	{
		errno = EINVAL;
		return -1;
	}
	
	ssize_t nbytes = min(bufsiz, newpath.length());
	memcpy(buf, newpath.bytes().ptr(), nbytes);
	return nbytes;
}

//I could keep track of what path this directory is for, or I could just tell Git that we don't know the filetype.
//The latter causes Git to fall back to stat (or, in this case, __xstat64), where I have the path easily available.
DLLEXPORT struct dirent* readdir(DIR* dirp);
DLLEXPORT struct dirent* readdir(DIR* dirp)
{
	dirent* r = readdir_o(dirp);
	if (r) r->d_type = DT_UNKNOWN;
	return r;
}
DLLEXPORT struct dirent64* readdir64(DIR* dirp);
DLLEXPORT struct dirent64* readdir64(DIR* dirp)
{
	dirent64* r = readdir64_o(dirp);
	if (r) r->d_type = DT_UNKNOWN;
	return r;
}
