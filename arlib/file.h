#pragma once
#include "global.h"
#include "string.h"
#include "array.h"

class file : nocopy {
public:
	class impl : nocopy {
	public:
		virtual size_t size() = 0;
		virtual bool resize(size_t newsize) = 0;
		
		virtual size_t pread(arrayvieww<byte> target, size_t start) = 0;
		virtual bool pwrite(arrayview<byte> data, size_t start = 0) = 0;
		virtual bool replace(arrayview<byte> data) { return resize(data.size()) && pwrite(data); }
		
		virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<byte> data) = 0;
		virtual arrayvieww<byte> mmapw(size_t start, size_t len) = 0;
		virtual bool unmapw(arrayvieww<byte> data) = 0;
		
		virtual ~impl() {}
		
		arrayview<byte> default_mmap(size_t start, size_t len);
		void default_unmap(arrayview<byte> data);
		arrayvieww<byte> default_mmapw(size_t start, size_t len);
		bool default_unmapw(arrayvieww<byte> data);
	};
	
	class implrd : public impl {
	public:
		virtual size_t size() = 0;
		bool resize(size_t newsize) { return false; }
		
		virtual size_t pread(arrayvieww<byte> target, size_t start) = 0;
		bool pwrite(arrayview<byte> data, size_t start = 0) { return false; }
		bool replace(arrayview<byte> data) { return false; }
		
		virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<byte> data) = 0;
		arrayvieww<byte> mmapw(size_t start, size_t len) { return NULL; }
		bool unmapw(arrayvieww<byte> data) { return false; }
	};
private:
	impl* core;
	size_t pos = 0;
	file(impl* core) : core(core) {}
	
public:
	enum mode {
		m_read,
		m_write,          // If the file exists, opens it. If it doesn't, creates a new file.
		m_wr_existing,    // Fails if the file doesn't exist.
		m_replace,        // If the file exists, it's either deleted and recreated, or truncated.
		m_create_excl,    // Fails if the file does exist.
	};
	
	file() : core(NULL) {}
	file(file&& f) { core = f.core; f.core = NULL; }
	file& operator=(file&& f) { delete core; core = f.core; f.core = NULL; return *this; }
	file(cstring filename, mode m = m_read) : core(NULL) { open(filename, m); }
	
	bool open(cstring filename, mode m = m_read)
	{
		delete core;
		core = open_impl(filename, m);
		return core;
	}
	void close()
	{
		delete core;
		core = NULL;
	}
	static file wrap(impl* core) { return file(core); }
	
private:
	//This one will create the file from the filesystem.
	//create() can simply return create_fs(filename), or can additionally support stuff like gvfs.
	static impl* open_impl_fs(cstring filename, mode m);
	//A path refers to a directory if it ends with a slash, and file otherwise. Directories may not be open()ed.
	static impl* open_impl(cstring filename, mode m);
public:
	
	operator bool() const { return core; }
	
	//Reading outside the file will return partial results.
	size_t size() const { return core->size(); }
	size_t pread(arrayvieww<byte> target, size_t start) const { return core->pread(target, start); }
	array<byte> readall() const
	{
		array<byte> ret;
		ret.reserve_noinit(this->size());
		size_t actual = this->pread(ret, 0);
		ret.resize(actual);
		return ret;
	}
	static array<byte> readall(cstring path)
	{
		file f(path);
		if (f) return f.readall();
		else return NULL;
	}
	
	bool resize(size_t newsize) { return core->resize(newsize); }
	//Writes outside the file will extend it with NULs.
	bool pwrite(arrayview<byte> data, size_t pos = 0) { return core->pwrite(data, pos); }
	//File pointer is undefined after calling this.
	bool replace(arrayview<byte> data) { return core->replace(data); }
	bool replace(cstring data) { return replace(data.bytes()); }
	bool pwrite(cstring data, size_t pos = 0) { return pwrite(data.bytes(), pos); }
	static bool writeall(cstring path, arrayview<byte> data)
	{
		file f(path, m_replace);
		return f.pwrite(data);
	}
	
	//Seeking outside the file is fine. This will return short reads, or extend the file 
	bool seek(size_t pos) { this->pos = pos; return true; }
	size_t tell() { return pos; }
	size_t read(arrayvieww<byte> data)
	{
		size_t ret = core->pread(data, pos);
		pos += ret;
		return ret;
	}
	bool write(arrayview<byte> data)
	{
		bool ok = core->pwrite(data, pos);
		if (ok) pos += data.size();
		return ok;
	}
	bool write(cstring data) { return write(data.bytes()); }
	
	//Mappings are not guaranteed to update if the underlying file changes. To force an update, delete and recreate the mapping.
	//If the underlying file is changed while a written mapping exists, it's undefined which (if any) writes take effect.
	//Resizing the file while a mapping exists is undefined behavior, including if the mapping is still in bounds (memimpl doesn't like that).
	//Mappings must be deleted before deleting the file object.
	arrayview<byte> mmap(size_t start, size_t len) const { return core->mmap(start, len); }
	arrayview<byte> mmap() const { return this->mmap(0, this->size()); }
	void unmap(arrayview<byte> data) const { return core->unmap(data); }
	
	arrayvieww<byte> mmapw(size_t start, size_t len) { return core->mmapw(start, len); }
	arrayvieww<byte> mmapw() { return this->mmapw(0, this->size()); }
	//If this succeeds, data written to the file is guaranteed to be written, assuming no other writes were made in the region.
	//If not, file contents are undefined in that range.
	//TODO: remove return value, replace with ->sync()
	//if failure is detected, set a flag to fail sync()
	//actually, make all failures trip sync(), both read/write/unmapw
	bool unmapw(arrayvieww<byte> data) { return core->unmapw(data); }
	
	~file() { delete core; }
	
	static file mem(arrayview<byte> data)
	{
		return file(new file::memimpl(data));
	}
	//the array may not be modified while the file object exists, other than via the file object itself
	static file mem(array<byte>& data)
	{
		return file(new file::memimpl(&data));
	}
private:
	class memimpl : public file::impl {
	public:
		arrayview<byte> datard;
		array<byte>* datawr; // this object does not own the array
		
		memimpl(arrayview<byte> data) : datard(data), datawr(NULL) {}
		memimpl(array<byte>* data) : datard(*data), datawr(data) {}
		
		size_t size() { return datard.size(); }
		bool resize(size_t newsize)
		{
			if (!datawr) return false;
			datawr->resize(newsize);
			datard = *datawr;
			return true;
		}
		
		size_t pread(arrayvieww<byte> target, size_t start)
		{
			size_t nbyte = min(target.size(), datard.size()-start);
			memcpy(target.ptr(), datard.slice(start, nbyte).ptr(), nbyte);
			return nbyte;
		}
		bool pwrite(arrayview<byte> newdata, size_t start = 0)
		{
			if (!datawr) return false;
			size_t nbyte = newdata.size();
			datawr->reserve_noinit(start+nbyte);
			memcpy(datawr->slice(start, nbyte).ptr(), newdata.ptr(), nbyte);
			datard = *datawr;
			return true;
		}
		bool replace(arrayview<byte> newdata)
		{
			if (!datawr) return false;
			*datawr = newdata;
			datard = *datawr;
			return true;
		}
		
		arrayview<byte>   mmap(size_t start, size_t len) { return datard.slice(start, len); }
		arrayvieww<byte> mmapw(size_t start, size_t len) { if (!datawr) return NULL; return datawr->slice(start, len); }
		void  unmap(arrayview<byte>  data) {}
		bool unmapw(arrayvieww<byte> data) { return true; }
	};
public:
	
	//Returns all items in the given directory path, as absolute paths.
	static array<string> listdir(cstring path);
	static bool unlink(cstring filename); // Returns whether the file is now gone. If the file didn't exist, returns true.
	//If the input path is a directory, the basename is blank.
	static string dirname(cstring path);
	static string basename(cstring path);
	
	//Returns whether the path is absolute.
	//On Unix, absolute paths start with /.
	//On Windows:
	// Absolute paths start with two slashes, or letter+colon+slash.
	// Drive-relative or rooted paths (/foo.txt, C:foo.txt) are considered invalid and are implementation-defined.
	// The path component separator is the forward slash on all operating systems, including Windows.
	static bool is_absolute(cstring path)
	{
#if defined(__unix__)
		return path[0]=='/';
#elif defined(_WIN32)
		if (path[0]=='/' && path[1]=='/') return true;
		if (path[1]==':' && path[2]=='/') return true;
		return false;
#else
#error unimplemented
#endif
	}
	
	//Removes all possible ./ and ../ components, and duplicate slashes, while still referring to the same file.
	//Similar to realpath(), but does not flatten symlinks.
	//foo/bar/../baz -> foo/baz, ./foo.txt -> foo.txt, ../foo.txt -> ../foo.txt, foo//bar.txt -> foo/bar.txt, . -> .
	//Invalid paths (above the root, or Windows half-absolute paths) are undefined behavior.
	static string resolve(cstring path);
	
	//Returns the path of the executable.
	//The cstring is owned by Arlib and lives forever.
	static cstring exepath();
private:
	static bool unlink_fs(cstring filename);
};


class autommap : public arrayview<byte> {
	const file& f;
public:
	autommap(const file& f, arrayview<byte> b) : arrayview(b), f(f) {}
	autommap(const file& f, size_t start, size_t end) : arrayview(f.mmap(start, end)), f(f) {}
	autommap(const file& f) : arrayview(f.mmap()), f(f) {}
	~autommap() { f.unmap(*this); }
};

class autommapw : public arrayvieww<byte> {
	file& f;
public:
	autommapw(file& f, arrayvieww<byte> b) : arrayvieww(b), f(f) {}
	autommapw(file& f, size_t start, size_t end) : arrayvieww(f.mmapw(start, end)), f(f) {}
	autommapw(file& f) : arrayvieww(f.mmapw()), f(f) {}
	~autommapw() { f.unmapw(*this); }
};
