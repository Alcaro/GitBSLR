// SPDX-License-Identifier: GPL-2.0-only
// GitBSLR is available under the same license as Git itself. If Git relicenses, you may choose
//    whether to use GitBSLR under GPL2 or Git's new license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
# define HAVE_DIRENT64
#else
# warning "Untested platform, please report whether it works: https://github.com/Alcaro/GitBSLR/issues/new"
#endif

class anyptr {
	void* data;
public:
	template<typename T> anyptr(T* data_) { data = (void*)data_; }
	template<typename T> operator T*() { return (T*)data; }
	template<typename T> operator const T*() const { return (const T*)data; }
};

#ifndef __GLIBC__
static const char * strchrnul(const char * s, int c)
{
	const char * ret = strchr(s, c);
	return ret ? ret : s+strlen(s);
}
#endif

template<typename T> static T min(const T& a, const T& b) { return a < b ? a : b; }

#define DLLEXPORT extern "C" __attribute__((__visibility__("default")))

static void malloc_fail()
{
	fprintf(stderr, "GitBSLR: out of memory\n");
	exit(1);
}

static anyptr malloc_check(size_t size)
{
	void* ret = malloc(size);
	if (size && !ret) malloc_fail();
	return ret;
}

static anyptr realloc_check(anyptr ptr, size_t size)
{
	void* ret = realloc(ptr, size);
	if (size && !ret) malloc_fail();
	return ret;
}

#define malloc malloc_check
#define realloc realloc_check

class string {
	char* ptr;
	size_t len;
	
	void set(const char * other, size_t len)
	{
		if (!len)
		{
			this->ptr = NULL;
			this->len = 0;
			return;
		}
		
		this->ptr = malloc(len+1);
		this->len = len;
		memcpy(ptr, other, len);
		ptr[len] = '\0';
	}
	void set(const char * other) { set(other, strlen(other)); }
	
public:
	string() { ptr = NULL; len = 0; }
	string(const string& other) { set(other.ptr, other.len); }
	string(const char * other) { set(other); }
	string(const char * other, size_t len) { set(other, len); }
	~string() { free(ptr); }
	
	operator const char *() const { return ptr ? ptr : ""; }
	const char * c_str() const { return ptr ? ptr : ""; }
	size_t length() const { return len; }
	operator bool() const { return len; }
	bool operator!() const { return len==0; }
	
	string& operator=(const char * other)
	{
		free(ptr);
		set(other);
		return *this;
	}
	string& operator=(const string& other)
	{
		free(ptr);
		set(other.ptr, other.len);
		return *this;
	}
	
	string& operator+=(const string& other)
	{
		ptr = realloc(ptr, len+other.len+1);
		strcpy(ptr+len, other.ptr);
		len += other.len;
		return *this;
	}
	
	string operator+(const string& other) const
	{
		string ret = *this;
		ret += other;
		return ret;
	}
	
	string operator+(const char * other) const
	{
		string ret = *this;
		ret += other;
		return ret;
	}
	
	bool operator==(const char * other) const
	{
		if (ptr) return !strcmp(ptr, other);
		else return (!other || !*other);
	}
	
	bool operator!=(const char * other) const
	{
		return !operator==(other);
	}
	
	static string create_usurp(char * str)
	{
		string ret;
		ret.ptr = str;
		ret.len = str ? strlen(str) : 0;
		return ret;
	}
	
	bool contains(const char * other) const
	{
		if (ptr) return strstr(ptr, other);
		else return (!other || !*other);
	}
	bool startswith(const char * other) const
	{
		if (ptr) return !memcmp(ptr, other, strlen(other));
		else return (!other || !*other);
	}
};



typedef int (*chdir_t)(const char * path);
typedef int (*lstat_t)(const char * path, struct stat* buf);
typedef int (*__lxstat64_t)(int ver, const char * path, struct stat64* buf);
typedef ssize_t (*readlink_t)(const char * path, char * buf, size_t bufsiz);
typedef struct dirent* (*readdir_t)(DIR* dirp);
typedef struct dirent64* (*readdir64_t)(DIR* dirp);
typedef int (*symlink_t)(const char * target, const char * linkpath);

static chdir_t chdir_o;
static lstat_t lstat_o;
static __lxstat64_t __lxstat64_o;
static readlink_t readlink_o;
static readdir_t readdir_o;
static readdir64_t readdir64_o;
static symlink_t symlink_o;


static string readlink_d(const string& path)
{
	size_t buflen = 64;
	char* buf = malloc(buflen);
	
again: ;
	ssize_t r = readlink_o(path.c_str(), buf, buflen);
	if (r <= 0) return "";
	if ((size_t)r >= buflen-1)
	{
		buflen *= 2;
		buf = realloc(buf, buflen);
		goto again;
	}
	
	buf[r] = '\0';
	return string::create_usurp(buf);
}

static string realpath_d(const string& path)
{
	return string::create_usurp(realpath(path.c_str(), NULL));
}

static string dirname_d(const string& path)
{
	const char * start = path;
	const char * last = strrchr(start, '/');
	return string(start, last-start+1);
}

//Input:
// A path to a symlink, both repo-relative and absolute, no trailing slash.
// (Yes, duplicate input is silly, but the caller already has easy access to both paths.)
//Output:
// Whether GITBSLR_FOLLOW says that path should be inlined. False = it's a link.
static bool link_force_inlined(const string& path_rel, const string& path_abs)
{
	const char * rules = getenv("GITBSLR_FOLLOW");
	if (!rules) return false;
	if (!*rules) return false;
	
	bool ret = false; // no matching rule -> default to keeping it as a link
	
	while (true)
	{
		const char * next = strchrnul(rules, ':');
		const char * end = next;
		
		bool ret_this = true;
		bool wildcard = false;
		if (*rules == '!') { rules++; ret_this = false; }
		
		if (*rules == ':')
		{
			fprintf(stderr, "GitBSLR: empty GITBSLR_FOLLOW entries are not allowed");
			exit(1);
		}
		
		// this intentionally accepts * as an entry
		if (end > rules && end[-1] == '*')
		{
			end--;
			wildcard = true;
			
			if (end > rules && end[-1] != '/')
			{
				fprintf(stderr, "GitBSLR: GITBSLR_FOLLOW entries can't end with * unless they end with /*");
				exit(1);
			}
		}
		
		if (end > rules && end[-1] == '/') end--; // ignore trailing slashes
		
		// keep running if the rule matches, so the last one wins
		if (memcmp(path_rel.c_str(), rules, end-rules)==0 && (wildcard || (size_t)(end-rules) == path_rel.length())) ret = ret_this;
		if (memcmp(path_abs.c_str(), rules, end-rules)==0 && (wildcard || (size_t)(end-rules) == path_abs.length())) ret = ret_this;
		
		if (!*next) return ret;
		rules = next+1;
	}
}

//Input:
// A file path, without trailing slash.
//Output:
// If that path should refer to a symlink, return what it points to, relative to the presumed link's parent directory.
// If it doesn't exist, or shouldn't be a symlink, return a blank string.
//The function may not call lstat or readlink, that'd yield infinite recursion. It may call readlink_o, which is the real readlink from libc.
static string resolve_symlink(string path)
{
	//algorithm:
	//if the path is inside .git/:
	// tell the truth
	//for each prefix of the path:
	// if path is the same thing as prefix (realpath identical):
	//  it's a link
	// if path is a link, points to inside prefix, and GITBSLR_FOLLOW doesn't say to inline it:
	//  it's a link (but check realpath of all prefixes to determine where it leads)
	//otherwise, it's not a link
	
	string path_linktarget = readlink_d(path);
	
	string root_abs = realpath_d(".");
	
	string path_abs = realpath_d(path); // if 'path' is a link, this refers to the link target
	if ((path_abs+"/").contains("/.git/")) return path_linktarget; // under .git -> return truth
	if (!path_abs) return ""; // nonexistent -> not a symlink (do not put above the .git check, git init creates a symlink to nonexistent)
	if (path == root_abs) return ""; // repo root is not a link; there can be links to repo root, but the actual absolute path is not.
	if (path.startswith(root_abs+"/"))
		path = string(path.c_str() + strlen(root_abs)+1); // if path is absolute and in the repo, turn it relative to repo root and check that
	
	if (path.startswith("/usr/share/git-core/")) return path_linktarget; // git likes reading some random stuff here, let it
	if (path[0] == '/')
	{
		fprintf(stderr, "GitBSLR: internal error, unexpected absolute path %s\n", path.c_str());
		exit(1);
	}
	
	
	const char * start = path;
	const char * iter = start;
	
	bool target_is_in_repo = false;
	
	while (true)
	{
		const char * next = strchrnul(iter+1, '/');
		
		string newpath = string(start, iter-start);
		if (newpath == "") newpath = ".";
		string newpath_abs = realpath_d(newpath);
		
		// if this path is the same as the link target,
		if (newpath_abs == path_abs)
		{
			// it's a link
			if (!*next) return ".";
			string ret;
			while (*next)
			{
				ret += "../";
				next = strchrnul(next+1, '/');
			}
			return string(ret, ret.length()-1);
		}
		
		//if it's originally a symlink, and points to inside the repo,
		//it's a candidate for inlining - but the above check overrides it, if necessary
		if (path_linktarget && path_abs.startswith(newpath_abs+"/"))
			target_is_in_repo = true;
		
		iter = next;
		
		if (!*iter)
		{
			if (!target_is_in_repo) return ""; // if it'd point outside the repo, it's not a link
			if (link_force_inlined(path, root_abs+"/"+path)) return ""; // if GITBSLR_FOLLOW says inline, it's not a link
			return path_linktarget;
		}
	}
}




static bool initialized = false;
static bool debug = false;

__attribute__((constructor)) static void init()
{
	chdir_o = (chdir_t)dlsym(RTLD_NEXT, "chdir");
	lstat_o = (lstat_t)dlsym(RTLD_NEXT, "lstat");
	__lxstat64_o = (__lxstat64_t)dlsym(RTLD_NEXT, "__lxstat64");
	readlink_o = (readlink_t)dlsym(RTLD_NEXT, "readlink");
	readdir_o = (readdir_t)dlsym(RTLD_NEXT, "readdir");
	readdir64_o = (readdir64_t)dlsym(RTLD_NEXT, "readdir64");
	symlink_o = (symlink_t)dlsym(RTLD_NEXT, "symlink");
	
	//GitBSLR shouldn't be loaded into the EDITOR
	unsetenv("LD_PRELOAD");
	
	if (getenv("GITBSLR_DEBUG"))
	{
		debug = true;
		fprintf(stderr, "GitBSLR: loaded\n");
	}
}


//the first thing Git does is find .git and chdir to it
//.. and .git are never symlinks and should never be, so before chdir is called, let's not override anything
DLLEXPORT int chdir(const char * path)
{
	if (debug) fprintf(stderr, "GitBSLR: chdir(%s)\n", path);
	
	initialized = true;
	return chdir_o(path);
}

DLLEXPORT int lstat(const char * path, struct stat* buf)
{
	if (!initialized || strstr(path, "/.git/")) // for git init
		return lstat_o(path, buf);
	
	int ret = stat(path, buf);
	if (ret<0) return ret;
	
	string newpath = resolve_symlink(path);
	if (debug) fprintf(stderr, "GitBSLR: lstat(%s)%s%s\n", path, newpath ? " -> " : "", newpath.c_str());
	if (newpath)
	{
		buf->st_mode &= ~S_IFMT;
		buf->st_mode |= S_IFLNK;
		buf->st_size = newpath.length();
	}
	//looking for the else clause to make it say 'no, it's not a link'? that's done by calling stat rather than lstat
	return ret;
}

#ifdef HAVE_DIRENT64
DLLEXPORT int __lxstat64(int ver, const char * path, struct stat64* buf)
{
	// http://refspecs.linuxbase.org/LSB_3.0.0/LSB-PDA/LSB-PDA/baselib-xstat64-1.html says version must be 3, but my Git uses 1
	// most likely struct stat64 changing - I don't really care what that struct is, I care only about which path to (l)stat,
	// so I can safely ignore the version
	
	if (!initialized || strstr(path, "/.git/")) // for git init
		return __lxstat64_o(ver, path, buf);
	
	int ret = __xstat64(ver, path, buf);
	if (ret<0) return ret;
	
	string newpath = resolve_symlink(path);
	if (debug) fprintf(stderr, "GitBSLR: __lxstat64(%s)%s%s\n", path, newpath ? " -> " : "", newpath.c_str());
	if (newpath)
	{
		buf->st_mode &= ~S_IFMT;
		buf->st_mode |= S_IFLNK;
		buf->st_size = newpath.length();
	}
	return ret;
}
#endif

DLLEXPORT ssize_t readlink(const char * path, char * buf, size_t bufsiz)
{
	if (!initialized) return readlink_o(path, buf, bufsiz);
	
	string newpath = resolve_symlink(path);
	if (debug) fprintf(stderr, "GitBSLR: readlink(%s) -> %s\n", path, newpath ? newpath.c_str() : "(not link)");
	if (!newpath)
	{
		errno = EINVAL;
		return -1;
	}
	
	ssize_t nbytes = min(bufsiz, newpath.length());
	memcpy(buf, (const char*)newpath, nbytes);
	return nbytes;
}

DLLEXPORT int symlink(const char * target, const char * linkpath)
{
	if (debug) fprintf(stderr, "GitBSLR: symlink(%s <- %s)\n", target, linkpath);
	
	if (strstr(linkpath, "/.git/"))
	{
		//git init (and clone) create a symlink at some random filename in .git to 'testing' for whatever reason. let it
		return symlink_o(target, linkpath);
	}
	
	string reporoot_abs = realpath_d(".");
	
	string target_tmp;
	if (target[0]=='/') target_tmp = target;
	else target_tmp = dirname_d(reporoot_abs+"/"+linkpath)+target;
	
	string target_abs = realpath_d(target_tmp);
	
	if (!target_abs)
	{
		//TODO: figure out what this should really do
fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since that target doesn't exist", linkpath, target);
//puts(string("A")+reporoot_abs);
//puts(string("B")+target);
//puts(string("C")+linkpath);
//puts(string("D")+target_tmp);
//puts(string("E")+target_abs);
errno = EPERM;
return -1;
		target_abs = realpath_d(dirname_d(target_tmp));
	}
	
	if ((target_abs+"/").contains("/.git/"))
	{
		fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since that's under .git/", linkpath, target);
		errno = EPERM;
		return -1;
	}
	else if (!reporoot_abs || !target_abs || (reporoot_abs != target_abs && !target_abs.startswith(reporoot_abs+"/")))
	{
		fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since %s is not under %s",
		                linkpath, target, target_abs.c_str(), reporoot_abs.c_str());
		errno = EPERM;
		return -1;
	}
	else
	{
		return symlink_o(target, linkpath);
	}
}

//I could hijack opendir and keep track of what path this DIR* is for, or I could tell Git that we don't know the filetype.
//The latter causes Git to fall back to some appropriate stat() variant, where I have the path easily available.
DLLEXPORT struct dirent* readdir(DIR* dirp)
{
	dirent* r = readdir_o(dirp);
	if (r) r->d_type = DT_UNKNOWN;
	return r;
}
#ifdef HAVE_DIRENT64
DLLEXPORT struct dirent64* readdir64(DIR* dirp)
{
	dirent64* r = readdir64_o(dirp);
	if (r) r->d_type = DT_UNKNOWN;
	return r;
}
#endif
