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
typedef int (*symlink_t)(const char *target, const char *linkpath);

static chdir_t chdir_o;
static readlink_t readlink_o;
static readdir_t readdir_o;
static readdir64_t readdir64_o;
static symlink_t symlink_o;


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

static string realpath_d(cstring path)
{
	return string::create_usurp(realpath(path.c_str(), NULL));
}

//Input:
// A path to a file, relative to the Git repo root, usable with stat() or readlink().
//Output:
// If that path should refer to a symlink, return what it points to (relative to the presumed link's parent directory).
// If it doesn't exist, or shouldn't be a symlink, return a blank string.
//The function may not call lstat or readlink, that'd yield infinite recursion. Instead, append _o and call that.
static string resolve_symlink(cstring path)
{
	//algorithm:
	//if the path is inside .git/:
	// tell the truth
	//for each prefix of the path:
	// if path is the same thing as prefix (realpath identical):
	//  it's a link
	// if path is a link, and points to inside prefix:
	//  it's a link
	//otherwise, it's not a link
	
	string path_linktarget = readlink_d(path);
	
	string root_abs = realpath_d(".");
	
	string path_abs = realpath_d(path);
	if (!path_abs) return ""; // nonexistent -> not a symlink
	if ((path_abs+"/").contains("/.git/")) return path_linktarget; // under .git -> return truth
	if (path == root_abs) return ""; // repo root is not a link; there can be links to repo root, but this one is not.
	
	if (path.startswith("/usr/share/git-core/")) return path_linktarget; // git likes reading some random stuff here, let it
	if (path[0] == '/')
	{
		puts("GitBSLR: internal error, unexpected absolute path "+path);
		exit(1);
	}
	
	
	array<cstring> parts = path.csplit("/");
	for (size_t i=0;i<parts.size();i++)
	{
		string newpath = parts.slice(0,i).join("/");
		if (newpath == "") newpath = ".";
		string newpath_abs = realpath_d(newpath);
		
		if (newpath_abs == path_abs)
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
	symlink_o = (symlink_t)dlsym(RTLD_NEXT, "symlink");
}


//the first thing Git does is find .git and chdir to it
//.. and .git are never symlinks and should never be, so before chdir is called, let's not override anything
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
	//looking for the else clause to make it say 'no, it's not a link'? that's done by calling stat rather than lstat
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

DLLEXPORT int symlink(const char * target, const char * linkpath);
DLLEXPORT int symlink(const char * target, const char * linkpath)
{
	if (strstr(linkpath, "/.git/"))
	{
		//git init (and clone) create a symlink at some random filename in .git to 'testing' for whatever reason. let it
		return symlink_o(target, linkpath);
	}
	
	string reporoot_abs = realpath_d(".");
	
	string target_abs;
	
	string target_tmp;
	if (target[0]=='/') target_tmp = target;
	else target_tmp = file::dirname(reporoot_abs+"/"+linkpath)+target;
	
	target_abs = realpath_d(target_tmp);
	
	if (!target_abs)
	{
		//TODO: figure out what this should really do
puts((string)"GitBSLR: link at "+linkpath+" is not allowed to point to "+target+
             ", since that target doesn't exist");
//puts(string("A")+reporoot_abs);
//puts(string("B")+target);
//puts(string("C")+linkpath);
//puts(string("D")+target_tmp);
//puts(string("E")+target_abs);
errno = EPERM;
return -1;
		target_abs = realpath_d(file::dirname(target_tmp));
	}
	
	if ((target_abs+"/").contains("/.git/"))
	{
		puts((string)"GitBSLR: link at "+linkpath+" is not allowed to point to "+target+
		             ", since that's under .git/");
		errno = EPERM;
		return -1;
	}
	else if (!reporoot_abs || !target_abs || (reporoot_abs != target_abs && !target_abs.startswith(reporoot_abs+"/")))
	{
		puts((string)"GitBSLR: link at "+linkpath+" is not allowed to point to "+target+
		             ", since "+target_abs+" is not under "+reporoot_abs);
		errno = EPERM;
		return -1;
	}
	else
	{
		return symlink_o(target, linkpath);
	}
}

//I could hijack opendir and keep track of what path this DIR* is for, or I could just tell Git that we don't know the filetype.
//The latter causes Git to fall back to some appropriate stat() variant, where I have the path easily available.
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
