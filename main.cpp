// SPDX-License-Identifier: GPL-2.0-only
// GitBSLR is available under the same license as Git itself. If Git relicenses, you may choose
//    whether to use GitBSLR under GPLv2 or Git's new license.

// Terminology:
// Git - obvious
// GitBSLR - this tool
// Work tree - where your repo is checked out
// Git directory - .git, usually in work tree
// Real path - a path as seen by the kernel
// Virtual path - a path as seen by Git (always relative to work tree)

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

#ifndef BUG_URL
#define BUG_URL "https://github.com/Alcaro/GitBSLR/issues"
#endif

#if defined(__linux__)
# define HAVE_STAT64 1
#else
# define HAVE_STAT64 0
# warning "Untested platform, please report whether it works: https://github.com/Alcaro/GitBSLR/issues"
#endif

#ifdef _STAT_VER
# define HAVE_STAT_VER 1
#else
# define HAVE_STAT_VER 0
#endif

// TODO: add a test for git clone
// I don't want tests to touch the network, but clones from local directories fail because unexpected access to <source repo location>
// not sure if that's fixable without creating a GITBSLR_THIRD_DIR env, and I don't know if I want to do that (needs a better name first)

#undef DEBUG
#define DEBUG(...) do { if (debug_level >= 1) fprintf(stderr, __VA_ARGS__); } while(0)
#define DEBUG_VERBOSE(...) do { if (debug_level >= 2) fprintf(stderr, __VA_ARGS__); } while(0)
#define FATAL(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(0)
static int debug_level = 0;


class anyptr {
	void* data;
public:
	template<typename T> anyptr(T* data_) { data = (void*)data_; }
	template<typename T> operator T*() { return (T*)data; }
	template<typename T> operator const T*() const { return (const T*)data; }
};

template<typename T> static T min(const T& a, const T& b) { return a < b ? a : b; }

#define DLLEXPORT extern "C" __attribute__((__visibility__("default")))

static void malloc_fail()
{
	FATAL("GitBSLR: out of memory\n");
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

#ifndef __GLIBC__
static const char * strchrnul(const char * s, int c)
{
	const char * ret = strchr(s, c);
	return ret ? ret : s+strlen(s);
}

static void * memrchr(const void * s, int c, size_t n)
{
	const uint8_t * s8 = (uint8_t*)s;
	while (--n)
	{
		if (s8[n] == c)
			return (void*)(s8+n);
	}
	return NULL;
}

static char* my_getcwd(char* buf, size_t size)
{
	if (buf) return getcwd(buf, size);
	
	size_t buflen = 64;
	char* buf = malloc(buflen);
	
	while (!getcwd(buf, buflen))
	{
		buflen *= 2;
		buf = realloc(buf, buflen);
	}
	
	buf[r] = '\0';
	return buf;
}
#define getcwd my_getcwd
#endif

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
		if (!other.len) return *this;
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
	bool endswith(const char * other) const
	{
		if (ptr) return !memcmp(ptr+len-strlen(other), other, strlen(other));
		else return (!other || !*other);
	}
};



typedef int (*lstat_t)(const char * path, struct stat* buf);
typedef ssize_t (*readlink_t)(const char * path, char * buf, size_t bufsiz);
typedef struct dirent* (*readdir_t)(DIR* dirp);
typedef int (*symlink_t)(const char * target, const char * linkpath);

static lstat_t lstat_o;
static readlink_t readlink_o;
static readdir_t readdir_o;
static symlink_t symlink_o;

#if HAVE_STAT_VER
typedef int (*__lxstat_t)(int ver, const char * path, struct stat* buf);
static __lxstat_t __lxstat_o;
#endif

#if HAVE_STAT64
typedef struct dirent64* (*readdir64_t)(DIR* dirp);
static readdir64_t readdir64_o;
typedef int (*lstat64_t)(const char * path, struct stat64* buf);
static lstat64_t lstat64_o;
#endif

#if HAVE_STAT64 && HAVE_STAT_VER
typedef int (*__lxstat64_t)(int ver, const char * path, struct stat64* buf);
static __lxstat64_t __lxstat64_o;
#endif

static inline void ensure_type_correctness()
{
	// If any of the above typedefs are incorrect, these will throw various compile errors.
	// If the functions don't exist at all, it will also throw errors, signaling that some of the HAVE_ checks are wrong.
	(void)(lstat_o == lstat);
	(void)(readlink_o == readlink);
	(void)(readdir_o == readdir);
	(void)(symlink_o == symlink);
#if HAVE_STAT_VER
	(void)(__lxstat == __lxstat_o);
#endif
#if HAVE_STAT64
	(void)(readdir64 == readdir64_o);
	(void)(lstat64_o == lstat64);
#endif
#if HAVE_STAT64 && HAVE_STAT_VER
	(void)(__lxstat64 == __lxstat64_o);
#endif
}


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
	if (path.endswith("/"))
	{
		return dirname_d(string(path, path.length()-1));
	}
	const char * start = path;
	const char * last = strrchr(start, '/');
	return string(start, last-start+1);
}



enum path_class_t {
	cls_git_dir, // or in /usr/share/git-core/
	cls_work_tree, // not necessarily actually in the work tree, could be hopping through a symlink to outside
	cls_unknown, // if fatal_unknown is true, this can't be returned; if it would be this, the program terminates instead
};
class path_handler {
public:
	// These two always end with slash, if configured.
	string work_tree;
	string git_dir;
	
	// Input may or may not have slash. Output will not have a slash.
	string parent_dir(const string& path)
	{
		const char * start = path.c_str();
		const char * end = (char*)memrchr((void*)path.c_str(), '/', path.length()-1);
		return string(start, end-start);
	}
	
	bool initialized() const { return work_tree; }
	
	// Paths may, but are not required to, end with a slash. However, they must be absolute.
	// Configuring the Git directory will also configure the work tree, if it's not set already.
	void set_git_dir(const string& dir)
	{
		if (dir.endswith("/")) git_dir = dir;
		else git_dir = dir+"/";
		
		if (!git_dir.endswith("/.git/"))
			FATAL("GitBSLR: The git directory path must end with .git, it can't be %s\n", git_dir.c_str());
		
		if (!work_tree)
		{
			set_work_tree(parent_dir(git_dir));
			DEBUG("GitBSLR: Using work tree %s (autodetected)\n", work_tree.c_str());
		}
	}
	
	void set_work_tree(const string& dir)
	{
		if (dir.endswith("/")) work_tree = dir;
		else work_tree = dir+"/";
	}
	
	// Call only on paths known to exist. If it contains a /.git/, the Git directory is configured. This may set the work tree.
	void try_init(const string& path)
	{
		if (git_dir)
			return;
		if (path.endswith("/.git"))
		{
			DEBUG("GitBSLR: Using git dir %s (autodetected)\n", path.c_str());
			set_git_dir(path);
			return;
		}
		if (path.contains("/.git/"))
		{
			const char * gitdir_start = path.c_str();
			const char * gitdir_end = strstr(gitdir_start, "/.git/") + strlen("/.git/");
			DEBUG("GitBSLR: Using git dir %.*s (autodetected)\n", (int)(gitdir_end-gitdir_start), gitdir_start);
			set_git_dir(string(gitdir_start, gitdir_end-gitdir_start));
			return;
		}
	}
	
	// This one does not consider GITBSLR_FOLLOW.
	// If the Git directory or work tree are not yet known, this function won't return that.
	path_class_t classify(const string& path, bool fatal_unknown) const
	{
		if (path[0] != '/')
			return classify(string::create_usurp(getcwd(NULL, 0)) + "/" + path, fatal_unknown);
		
		if (path.startswith("/usr/share/git-core/"))
			return cls_git_dir;
		if (git_dir && path.startswith(git_dir))
			return cls_git_dir;
		if (git_dir && path+"/" == git_dir)
			return cls_git_dir;
		if (work_tree && path.startswith(work_tree))
			return cls_work_tree;
		if (work_tree && path+"/" == work_tree)
			return cls_work_tree;
		if (fatal_unknown)
		{
			if (!git_dir || !work_tree)
				FATAL("GitBSLR: unexpected access to %s before locating Git directory and/or work tree. "
				      "Either you're missing GITBSLR_GIT_DIR and/or GITBSLR_WORK_TREE, or you found a GitBSLR bug. "
				      "If latter, please report it: " BUG_URL "\n",
				      path.c_str());
			else
				FATAL("GitBSLR: unexpected access to %s; should only be in %s or %s. "
				      "Either you're missing GITBSLR_GIT_DIR and/or GITBSLR_WORK_TREE, or you found a GitBSLR bug. "
				      "If latter, please report it: " BUG_URL "\n",
				      path.c_str(), work_tree.c_str(), git_dir.c_str());
		}
		return cls_unknown;
	}
	
	bool is_in_git_dir(const string& path) const { return classify(path, true) == cls_git_dir; }
	
	//Input: A path to a symlink, relative to the current directory, no trailing slash.
	//Output: Whether GITBSLR_FOLLOW says that path should be inlined. False = it's a link.
	bool link_force_inline(const string& path) const
	{
		const char * rules = getenv("GITBSLR_FOLLOW");
		if (!rules || !*rules) return false;
		
		string cwd = string::create_usurp(getcwd(NULL, 0))+"/";
		if (!cwd.startswith(work_tree))
			FATAL("GitBSLR: current directory %s should be in work tree %s\n", cwd.c_str(), work_tree.c_str());
		
		//path is relative to cwd
		//path_rel is relative to work tree
		//path_abs is work tree plus path_rel
		
		string path_rel = string(cwd.c_str()+work_tree.length(), cwd.length()-work_tree.length()) + path;
		string path_abs = work_tree + path_rel;
		
		bool ret = false; // no matching rule -> default to keeping it as a link
		
		while (true)
		{
			const char * next = strchrnul(rules, ':');
			const char * end = next;
			
			bool ret_this = true;
			bool wildcard = false;
			if (*rules == '!') { rules++; ret_this = false; }
			
			if (*rules == ':')
				FATAL("GitBSLR: empty GITBSLR_FOLLOW entries are not allowed");
			
			// this intentionally accepts * as an entry
			if (end > rules && end[-1] == '*')
			{
				end--;
				wildcard = true;
				
				if (end > rules && end[-1] != '/')
					FATAL("GitBSLR: GITBSLR_FOLLOW entries can't end with * unless they end with /*");
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
	// Any virtual path.
	//Output:
	// If that path should refer to a symlink, return what it points to, relative to the presumed link's parent directory.
	// If it doesn't exist, or shouldn't be a symlink, return a blank string.
	//The function may not call lstat or readlink, that'd yield infinite recursion. It may call readlink_o, which is the real readlink.
	string resolve_symlink(string path) const
	{
		//algorithm:
		//if the path is inside git directory:
		// tell the truth
		//for each prefix of the path:
		// if path is the same thing as prefix (realpath identical):
		//  it's a link
		// if path is a link, points to inside prefix, and GITBSLR_FOLLOW doesn't say to inline it:
		//  it's a link (but check realpath of all prefixes to determine where it leads)
		//otherwise, it's not a link
		
		string path_linktarget = readlink_d(path);
		
		string root_abs = realpath_d(".");
		if (root_abs+"/" != work_tree)
			FATAL("GitBSLR: internal error, attempted symlink check while cwd != worktree. Please report this bug: " BUG_URL "\n");
		
		string path_abs = realpath_d(path); // if 'path' is a link, this refers to the link target
		if (!path_abs) return ""; // nonexistent -> not a symlink
		if ((path_abs+"/").startswith(git_dir)) return path_linktarget; // git dir -> return truth
		if (path.startswith("/usr/share/git-core/")) return path_linktarget; // git likes reading some random stuff here, let it
		if (path == root_abs) return ""; // telling git that work tree is a link won't end well
		if (path.startswith(work_tree)) // if path is absolute and in work tree, discard work tree prefix and turn it relative
			path = string(path.c_str() + strlen(root_abs)+1);
		
		if (path[0] == '/')
			FATAL("GitBSLR: internal error, unexpected absolute path %s. Please report this bug: " BUG_URL "\n", path.c_str());
		
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
				if (link_force_inline(path)) return ""; // if GITBSLR_FOLLOW says inline, it's not a link
				
				// if the link's realpath is not in the work dir, but the target is, ignore readlink and create a new path
				if (!newpath_abs.startswith(work_tree))
				{
					// path is virtual path to link
					// path_abs is real path to link, including work tree
					string& source_virt = path; // rename this variable
					string target_virt = string(path_abs.c_str() + strlen(root_abs)+1);
					
					// if <wtree>/a/b/c points to <wtree>/a/d, emit ../d, not ../../a/d
					size_t start = 0;
					for (size_t i=0;source_virt[i] == target_virt[i];i++)
					{
						if (source_virt[i] == '/') start = i+1;
					}
					
					string up;
					const char * next = strchr(source_virt.c_str()+start, '/');
					while (next)
					{
						up += "../";
						next = strchr(next+1, '/');
					}
					return up + (target_virt.c_str()+start);
				}
				else
				{
					return path_linktarget;
				}
			}
		}
	}
};
static path_handler gitpath;



#if HAVE_STAT_VER
static int lstat_lxstat_wrap(const char * path, struct stat * buf)
{
	return __lxstat_o(_STAT_VER, path, buf);
}
#endif
#if HAVE_STAT64 && HAVE_STAT_VER
static int lstat64_lxstat_wrap(const char * path, struct stat64 * buf)
{
	return __lxstat64_o(_STAT_VER, path, buf);
}
#endif
__attribute__((constructor)) static void init()
{
	if (getenv("GITBSLR_DEBUG"))
	{
		char * end;
		debug_level = strtol(getenv("GITBSLR_DEBUG"), &end, 0);
		if (*end) debug_level = 1;
		DEBUG("GitBSLR: Loaded\n");
	}
	
	lstat_o = (lstat_t)dlsym(RTLD_NEXT, "lstat");
#if HAVE_STAT_VER
	if (!lstat_o)
	{
		__lxstat_o = (__lxstat_t)dlsym(RTLD_NEXT, "__lxstat");
		if (__lxstat_o) lstat_o = lstat_lxstat_wrap;
	}
#endif
	readlink_o = (readlink_t)dlsym(RTLD_NEXT, "readlink");
	readdir_o = (readdir_t)dlsym(RTLD_NEXT, "readdir");
	symlink_o = (symlink_t)dlsym(RTLD_NEXT, "symlink");
	
#if HAVE_STAT64
	readdir64_o = (readdir64_t)dlsym(RTLD_NEXT, "readdir64");
	lstat64_o = (lstat64_t)dlsym(RTLD_NEXT, "lstat64");
#if HAVE_STAT_VER
	if (!lstat64_o)
	{
		__lxstat64_o = (__lxstat64_t)dlsym(RTLD_NEXT, "__lxstat64");
		if (__lxstat64_o) lstat64_o = lstat64_lxstat_wrap;
	}
#endif
#endif
	
	if (!lstat_o || !readlink_o || !readdir_o || !symlink_o
#if HAVE_STAT64
		|| !readdir64_o || !lstat64_o
#endif
		)
		FATAL("GitBSLR: couldn't dlsym required symbols (this is a GitBSLR bug, please report it: " BUG_URL ")\n");
	
	// GitBSLR shouldn't be loaded into the EDITOR
	unsetenv("LD_PRELOAD");
	
	// if this env is set and the entire repo is behind a symlink, Git occasionally accesses it via the link instead
	// GitBSLR will see this as access to an unrelated path and ask for a bug report
	unsetenv("PWD");
	
	const char * gitbslr_git_dir = getenv("GITBSLR_GIT_DIR");
	if (gitbslr_git_dir)
	{
		gitpath.set_git_dir(gitbslr_git_dir);
		DEBUG("GitBSLR: Using git dir %s (from env)\n", gitbslr_git_dir);
		setenv("GIT_DIR", gitbslr_git_dir, true);
	}
	else if (getenv("GIT_DIR"))
		FATAL("GitBSLR: use GITBSLR_GIT_DIR, not GIT_DIR\n");
	
	const char * gitbslr_work_tree = getenv("GITBSLR_WORK_TREE");
	if (gitbslr_work_tree)
	{
		gitpath.set_work_tree(gitbslr_work_tree);
		DEBUG("GitBSLR: Using work tree %s (from env)\n", gitbslr_work_tree);
		setenv("GIT_WORK_TREE", gitbslr_work_tree, true);
	}
	else if (getenv("GIT_WORK_TREE"))
		FATAL("GitBSLR: use GITBSLR_WORK_TREE, not GIT_WORK_TREE\n");
}



static int stat_3264(const char * path, struct stat* buf)
{
	return stat(path, buf);
}
static int lstat_o_3264(const char * path, struct stat* buf)
{
	return lstat_o(path, buf);
}
#if HAVE_STAT64
static int stat_3264(const char * path, struct stat64* buf)
{
	return stat64(path, buf);
}
static int lstat_o_3264(const char * path, struct stat64* buf)
{
	return lstat64_o(path, buf);
}
#endif

template<typename stat_t>
int inner_lstat(const char * fn_name, const char * path, stat_t* buf)
{
	DEBUG_VERBOSE("GitBSLR: %s(%s)\n", fn_name, path);
	if (!gitpath.initialized() || gitpath.is_in_git_dir(path))
	{
		DEBUG("GitBSLR: %s(%s) - untouched because %s\n", fn_name, path, gitpath.initialized() ? "in .git" : ".git not yet located");
		int ret = lstat_o_3264(path, buf);
		int errno_tmp = errno;
		if (ret >= 0) gitpath.try_init(path);
		errno = errno_tmp;
		return ret;
	}
	
	int ret = stat_3264(path, buf);
	if (ret < 0)
	{
		DEBUG("GitBSLR: %s(%s) - untouched because can't stat (%s)\n", fn_name, path, strerror(errno));
		return lstat_o_3264(path, buf);
	}
	
	string newpath = gitpath.resolve_symlink(path);
	if (newpath) DEBUG("GitBSLR: %s(%s) -> %s\n", fn_name, path, newpath.c_str());
	else DEBUG("GitBSLR: %s(%s) - not a link\n", fn_name, path);
	if (newpath)
	{
		buf->st_mode &= ~S_IFMT;
		buf->st_mode |= S_IFLNK;
		buf->st_size = newpath.length();
	}
	// looking for the else clause to make it say 'no, it's not a link'? that's done by calling stat rather than lstat
	return ret;
}

DLLEXPORT int lstat(const char * path, struct stat* buf)
{
	return inner_lstat("lstat", path, buf);
}

DLLEXPORT int __lxstat(int ver, const char * path, struct stat* buf)
{
	// according to <http://refspecs.linuxbase.org/LSB_3.0.0/LSB-PDA/LSB-PDA/baselib-xstat64-1.html>,
	//  ver should be 3, but _STAT_VER is 1
	// no clue what it's doing
	// and glibc 2.33 deletes _STAT_VER from the headers
#if HAVE_STAT_VER
	if (ver != _STAT_VER)
		FATAL("GitBSLR: git called __lxstat(%s) with wrong version (got %d, expected %d)\n", path, ver, _STAT_VER);
	
	return inner_lstat("__lxstat", path, buf);
#else
	FATAL("GitBSLR: git unexpectedly called __lxstat; are Git and GitBSLR compiled against different libc?\n");
#endif
}

#if HAVE_STAT64
DLLEXPORT int lstat64(const char * path, struct stat64* buf)
{
	return inner_lstat("lstat64", path, buf);
}

DLLEXPORT int __lxstat64(int ver, const char * path, struct stat64* buf)
{
#if HAVE_STAT_VER
	if (ver != _STAT_VER)
		FATAL("GitBSLR: git called __lxstat64(%s) with wrong version (got %d, expected %d)\n", path, ver, _STAT_VER);
	return inner_lstat("__lxstat64", path, buf);
	
#else
	FATAL("GitBSLR: git unexpectedly called __lxstat64; are Git and GitBSLR compiled against different libc?\n");
#endif
}
#else
DLLEXPORT int lstat64(const char * path, void* buf)
{
	FATAL("GitBSLR: git unexpectedly called lstat64; are Git and GitBSLR compiled against different libc?\n");
}

DLLEXPORT int __lxstat64(int ver, const char * path, void* buf)
{
	FATAL("GitBSLR: git unexpectedly called __lxstat64; are Git and GitBSLR compiled against different libc?\n");
}
#endif

DLLEXPORT ssize_t readlink(const char * path, char * buf, size_t bufsiz)
{
	DEBUG_VERBOSE("GitBSLR: readlink(%s)\n", path);
	if (!gitpath.initialized() || gitpath.is_in_git_dir(path))
	{
		DEBUG("GitBSLR: readlink(%s) - untouched because %s\n", path, gitpath.initialized() ? "in .git" : ".git not yet located");
		return readlink_o(path, buf, bufsiz);
	}
	
	string newpath = gitpath.resolve_symlink(path);
	DEBUG("GitBSLR: readlink(%s) -> %s\n", path, newpath ? newpath.c_str() : "(not link)");
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
	DEBUG_VERBOSE("GitBSLR: symlink(%s <- %s)\n", target, linkpath);
	
	if (strstr(linkpath, "/.git/"))
	{
		// git init (and clone) create a symlink at some random filename in .git to 'testing', to check if that works. let it
		return symlink_o(target, linkpath);
	}
	if ((string("/")+target+"/").contains("/.git/")) // make sure to reject all .git, not just current gitdir
	{
		fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since that's under .git/\n", linkpath, target);
		errno = EPERM;
		return -1;
	}
	if (!gitpath.work_tree)
	{
		fprintf(stderr, "GitBSLR: cannot create symlinks before finding the work tree (this is a GitBSLR bug, please report it: "
		                BUG_URL ")");
		errno = EPERM;
		return -1;
	}
	
	if (linkpath[0] == '/')
	{
		fprintf(stderr, "GitBSLR: link at %s is not allowed to point to absolute path %s\n", linkpath, target);
		errno = EPERM;
		return -1;
	}
	
	int n_leading_up = 0;
	while (memcmp(target + n_leading_up*3, "../", 3) == 0)
		n_leading_up++;
	if ((string(target + n_leading_up*3)+"/").contains("/../"))
	{
		fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s; ../ components must be at the start\n", linkpath, target);
		errno = EPERM;
		return -1;
	}
	
	// the work tree, and every symlink, is one-way; links may not point up past them
	string linkpath_abs = realpath_d(".")+"/"+linkpath;
	for (int i=0;i<=n_leading_up;i++)
	{
		linkpath_abs = dirname_d(linkpath_abs);
		
		struct stat buf;
		if (lstat_o(linkpath_abs, &buf) < 0)
		{
			int errno_tmp = errno;
			fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since %s is inaccessible (%s)\n",
			                linkpath, target, linkpath_abs.c_str(), strerror(errno_tmp));
			errno = errno_tmp;
			return -1;
		}
		if (S_ISLNK(buf.st_mode))
		{
			fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since %s is a symlink\n",
			                linkpath, target, linkpath_abs.c_str());
			errno = EPERM;
			return -1;
		}
	}
	
	if (!(linkpath_abs+"/").startswith(gitpath.work_tree))
	{
		fprintf(stderr, "GitBSLR: link at %s is not allowed to point to %s, since %s is not under %s\n",
		                linkpath, target, linkpath_abs.c_str(), gitpath.work_tree.c_str());
		errno = EPERM;
		return -1;
	}
	
	DEBUG("GitBSLR: symlink(%s <- %s) - creating\n", target, linkpath);
	return symlink_o(target, linkpath);
}

// I could hijack opendir and keep track of what path this DIR* is for, or I could tell Git that we don't know the filetype.
// The latter causes Git to fall back to some appropriate stat() variant, where I have the path easily available.
DLLEXPORT struct dirent* readdir(DIR* dirp)
{
	dirent* r = readdir_o(dirp);
	if (r) r->d_type = DT_UNKNOWN;
	return r;
}
#if HAVE_STAT64
DLLEXPORT struct dirent64* readdir64(DIR* dirp)
{
	dirent64* r = readdir64_o(dirp);
	if (r) r->d_type = DT_UNKNOWN;
	return r;
}
#else
DLLEXPORT void* readdir64(DIR* dirp)
{
	FATAL("GitBSLR: git unexpectedly called readdir64; are Git and GitBSLR compiled against different libc?\n");
}
#endif
