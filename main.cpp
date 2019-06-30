// SPDX-License-Identifier: GPL-2.0-only
// GitBSLR is available under the same license as Git itself. If Git relicenses, you may choose
//    whether to use GitBSLR under GPLv2 or Git's new license.

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

// TODO: add a test for git clone
// I don't want it to touch the network, but clones from local directories fail becaue unexpected access to <source repo location>
// not sure if that's fixable without creating a GITBSLR_THIRD_DIR env, and I don't know if I want to do that (needs a better name first)

#undef DEBUG
#define DEBUG(...) do { if (debug) fprintf(stderr, __VA_ARGS__); } while(0)
#define FATAL(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(0)
static bool debug = false;


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

#ifdef HAVE_DIRENT64
typedef int (*__lxstat64_t)(int ver, const char * path, struct stat64* buf);
typedef struct dirent64* (*readdir64_t)(DIR* dirp);
static __lxstat64_t __lxstat64_o;
static readdir64_t readdir64_o;
#endif

inline void ensure_type_correctness()
{
	//If any of the above typedefs are incorrect, these will throw various compile errors.
	(void)(lstat_o == lstat);
	(void)(readlink_o == readlink);
	(void)(readdir_o == readdir);
	(void)(symlink_o == symlink);
#ifdef HAVE_DIRENT64
	(void)(__lxstat64 == __lxstat64_o);
	(void)(readdir64 == readdir64_o);
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
	// These two always end with slash, if configured.
	string work_tree;
	string git_dir;
	
	//Input may or may not have slash. Output will not have a slash.
	string parent_dir(const string& path)
	{
		const char * start = path.c_str();
		const char * end = (char*)memrchr((void*)path.c_str(), '/', path.length()-1);
		return string(start, end-start);
	}
	
public:
	bool initialized() const { return work_tree; }
	
	// Paths may, but are not required to, end with a slash. However, they must be absolute.
	// Configuring the Git directory will configure the work tree, if it's not set already.
	void set_git_dir(const string& dir)
	{
		if (dir.endswith("/")) git_dir = dir;
		else git_dir = dir+"/";
		
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
	// If the Git directory or work tree are not yet known, this function configures them.
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
			FATAL("GitBSLR: unexpected access to %s; should only be in %s or %s. "
			      "Either you're missing GITBSLR_GIT_DIR and/or GITBSLR_WORK_TREE, or you found a GitBSLR bug.\n",
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
	// Any path within the work tree.
	//Output:
	// If that path should refer to a symlink, return what it points to, relative to the presumed link's parent directory.
	// If it doesn't exist, or shouldn't be a symlink, return a blank string.
	//The function may not call lstat or readlink, that'd yield infinite recursion. It may call readlink_o, which is the real readlink.
	string resolve_symlink(string path) const
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
		
		//this is wrong if the link is behind another link and the target is somewhere other than what Git expects,
		// but that's rare, low-impact, and hard to find a good algorithm for, so I'll just leave it unimplemented
		
		string path_linktarget = readlink_d(path);
		
		string root_abs = realpath_d(".");
		if (root_abs+"/" != work_tree)
			FATAL("GitBSLR: internal error, attempted symlink check while cwd != worktree. Please report this bug.\n");
		
		string path_abs = realpath_d(path); // if 'path' is a link, this refers to the link target
		if (!path_abs) return ""; // nonexistent -> not a symlink
		if ((path_abs+"/").startswith(git_dir)) return path_linktarget; // under .git -> return truth
		if (path.startswith("/usr/share/git-core/")) return path_linktarget; // git likes reading some random stuff here, let it
		if (path == root_abs) return ""; // repo root is not a link; repo root can be linked, but the actual absolute path isn't a link
		if (path.startswith(root_abs+"/")) // if path is absolute and in the repo, turn it relative to repo root and check that
			path = string(path.c_str() + strlen(root_abs)+1);
		
		if (path[0] == '/')
			FATAL("GitBSLR: internal error, unexpected absolute path %s\n", path.c_str());
		
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
				return path_linktarget;
			}
		}
	}
};
static path_handler gitpath;



__attribute__((constructor)) static void init()
{
	lstat_o = (lstat_t)dlsym(RTLD_NEXT, "lstat");
	readlink_o = (readlink_t)dlsym(RTLD_NEXT, "readlink");
	readdir_o = (readdir_t)dlsym(RTLD_NEXT, "readdir");
	symlink_o = (symlink_t)dlsym(RTLD_NEXT, "symlink");
#ifdef HAVE_DIRENT64
	__lxstat64_o = (__lxstat64_t)dlsym(RTLD_NEXT, "__lxstat64");
	readdir64_o = (readdir64_t)dlsym(RTLD_NEXT, "readdir64");
#endif
	
	//GitBSLR shouldn't be loaded into the EDITOR
	unsetenv("LD_PRELOAD");
	
	if (getenv("GITBSLR_DEBUG"))
	{
		debug = true;
		DEBUG("GitBSLR: Loaded\n");
	}
	
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


DLLEXPORT int lstat(const char * path, struct stat* buf)
{
	DEBUG("GitBSLR: lstat(%s)\n", path);
	if (!gitpath.initialized() || gitpath.is_in_git_dir(path))
	{
		DEBUG("GitBSLR: lstat(%s) - untouched because %s\n", path, gitpath.initialized() ? "in .git" : ".git not yet located");
		int ret = lstat_o(path, buf);
		int errno_tmp = errno;
		if (ret >= 0) gitpath.try_init(path);
		errno = errno_tmp;
		return ret;
	}
	
	int ret = stat(path, buf);
	if (ret < 0)
	{
		int errno_tmp = errno;
		DEBUG("GitBSLR: lstat(%s) - untouched because can't stat (%s)\n", path, strerror(errno_tmp));
		errno = errno_tmp;
		return ret;
	}
	
	string newpath = gitpath.resolve_symlink(path);
	DEBUG("%s%s\n", newpath ? " -> " : "", newpath.c_str());
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
	// according to <http://refspecs.linuxbase.org/LSB_3.0.0/LSB-PDA/LSB-PDA/baselib-xstat64-1.html>,
	// the version should be 3, but my Git uses 1
	// probably struct stat64 changing - I don't really care about that struct, I care only about which path to (l)stat,
	// so I'll ignore the version
	
	DEBUG("GitBSLR: __lxstat64(%s)\n", path);
	if (!gitpath.initialized() || gitpath.is_in_git_dir(path))
	{
		DEBUG("GitBSLR: __lxstat64(%s) - untouched because %s\n", path, gitpath.initialized() ? "in .git" : ".git not yet located");
		int ret = __lxstat64_o(ver, path, buf);
		int errno_tmp = errno;
		if (ret >= 0) gitpath.try_init(path);
		errno = errno_tmp;
		return ret;
	}
	
	int ret = __xstat64(ver, path, buf);
	if (ret < 0)
	{
		int errno_tmp = errno;
		DEBUG("GitBSLR: __lxstat64(%s) - untouched because can't stat (%s)\n", path, strerror(errno_tmp));
		errno = errno_tmp;
		return ret;
	}
	
	string newpath = gitpath.resolve_symlink(path);
	DEBUG("%s%s\n", newpath ? " -> " : "", newpath.c_str());
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
	DEBUG("GitBSLR: readlink(%s)\n", path);
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
	//TODO: rewrite this function, use more gitpath
	//needs more robust tests first
	//also make sure to reject targets containing /.git/,
	// even if that's a .git other than current work tree - can't have repos corrupt each other
	DEBUG("GitBSLR: symlink(%s <- %s)\n", target, linkpath);
	
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
