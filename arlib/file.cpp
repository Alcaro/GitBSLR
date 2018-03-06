#include "file.h"
#include "test.h"

arrayview<byte> file::impl::default_mmap(size_t start, size_t len)
{
	arrayvieww<byte> ret(malloc(len), len);
	size_t actual = this->pread(ret, start);
	return ret.slice(0, actual);
}

void file::impl::default_unmap(arrayview<byte> data)
{
	free((void*)data.ptr());
}

arrayvieww<byte> file::impl::default_mmapw(size_t start, size_t len)
{
	byte* ptr = malloc(sizeof(size_t) + len);
	*(size_t*)ptr = start;
	
	arrayvieww<byte> ret(ptr+sizeof(size_t), len);
	size_t actual = this->pread(ret, start);
	return ret.slice(0, actual);
}

bool file::impl::default_unmapw(arrayvieww<byte> data)
{
	byte* ptr = data.ptr() - sizeof(size_t);
	size_t start = *(size_t*)ptr;
	bool ok = this->pwrite(data, start);
	free(ptr);
	return ok;
}

string file::resolve(cstring path)
{
	array<cstring> parts = path.csplit("/");
	for (size_t i=0;i<parts.size();i++)
	{
		if (parts[i] == "" && i>0 && parts[i-1] != "")
		{
			parts.remove(i);
			i--;
			continue;
		}
		
		if (parts[i] == ".")
		{
			parts.remove(i);
			i--;
			continue;
		}
		
		if (parts[i] == ".." && i>0 && parts[i-1] != "..")
		{
			parts.remove(i);
			if (i>0) parts.remove(i-1);
			i-=2;
			continue;
		}
	}
	if (!parts) return ".";
	return parts.join("/");
}

#ifdef ARLIB_TEST

//criteria for READONLY_FILE:
//- must be a normal file, no /dev/*
//- minimum 66000 bytes
//- the first few bytes must be known, no .txt files or possibly-shebanged stuff
//- the file must contain somewhat unpredictable data, no huge streams of the same thing like /dev/zero
//- must be readable by everyone (assuming absense of sandboxes)
//- must NOT be writable or deletable by this program
//- no funny symbols in the name
//recommended choice: some random executable

//criteria for WRITABLE_FILE:
//- must not exist under normal operation
//- directory must exist
//- directory must be writable by unprivileged users
//- no funny symbols in the name
#ifdef _WIN32
#define READONLY_FILE "C:/Windows/notepad.exe" // screw anything where the windows directory isn't on C:
#define READONLY_FILE_HEAD "MZ"
#define WRITABLE_FILE "C:/Temp/arlib-selftest.txt"
#else
#define READONLY_FILE "/bin/sh"
#define READONLY_FILE_HEAD "\x7F""ELF"
#define WRITABLE_FILE "/tmp/arlib-selftest.txt"
#endif

test("file reading", "array", "file")
{
	file f;
	assert(f.open(READONLY_FILE));
	assert(f.size());
	assert(f.size() > strlen(READONLY_FILE_HEAD));
	assert(f.size() >= 66000);
	array<byte> bytes = f.readall();
	assert(bytes.size() == f.size());
	assert(!memcmp(bytes.ptr(), READONLY_FILE_HEAD, strlen(READONLY_FILE_HEAD)));
	
	arrayview<byte> map = f.mmap();
	assert(map.ptr());
	assert(map.size() == f.size());
	assert(!memcmp(bytes.ptr(), map.ptr(), bytes.size()));
	
	arrayview<byte> map2 = f.mmap();
	assert(map2.ptr());
	assert(map2.size() == f.size());
	assert(!memcmp(bytes.ptr(), map2.ptr(), bytes.size()));
	f.unmap(map2);
	
	const size_t t_start[] = { 0,     65536, 4096, 1,     1,     1,     65537, 65535 };
	const size_t t_len[]   = { 66000, 400,   400,  65535, 65536, 65999, 400,   2     };
	for (size_t i=0;i<ARRAY_SIZE(t_start);i++)
	{
		arrayview<byte> map3 = f.mmap(t_start[i], t_len[i]);
		assert(map3.ptr());
		assert(map3.size() == t_len[i]);
		assert(!memcmp(bytes.ptr()+t_start[i], map3.ptr(), t_len[i]));
		f.unmap(map3);
	}
	
	f.unmap(map);
	
	assert(!f.open(file::dirname(READONLY_FILE))); // opening a directory should fail
	assert(file::dirname(READONLY_FILE).endswith("/"));
	
#ifdef _WIN32
	assert(SetCurrentDirectory("C:/Windows/"));
	assert(f.open("notepad.exe"));
	assert(f.open("C:/Windows/notepad.exe"));
	//make sure these two are rejected, they're corrupt and have always been
	assert(!f.open("C:notepad.exe"));
	assert(!f.open("/Windows/notepad.exe"));
#endif
}

test("file writing", "array", "file")
{
	file f;
	
	assert(!f.open(READONLY_FILE, file::m_wr_existing)); // keep this first, to ensure it doesn't shred anything if we're run as root
	assert(!f.open(READONLY_FILE, file::m_write));
	assert(!f.open(READONLY_FILE, file::m_replace));
	assert(!f.open(READONLY_FILE, file::m_create_excl));
	
	assert( file::unlink(WRITABLE_FILE));
	assert(!file::unlink(READONLY_FILE));
	
	assert(!f.open(WRITABLE_FILE));
	
	assert(f.open(WRITABLE_FILE, file::m_write));
	assert(f.replace("foobar"));
	
	assert_eq(string(file::readall(WRITABLE_FILE)), "foobar");
	
	assert(f.resize(3));
	assert_eq(f.size(), 3);
	assert_eq(string(file::readall(WRITABLE_FILE)), "foo");
	
	assert(f.resize(8));
	assert_eq(f.size(), 8);
	byte expected[8]={'f','o','o',0,0,0,0,0};
	array<byte> actual = file::readall(WRITABLE_FILE);
	assert(actual.ptr());
	assert_eq(actual.size(), 8);
	assert(!memcmp(actual.ptr(), expected, 8));
	
	arrayvieww<byte> map = f.mmapw();
	assert(map.ptr());
	assert_eq(map.size(), 8);
	assert(!memcmp(map.ptr(), expected, 8));
	map[3]='t';
	f.unmapw(map);
	
	expected[3] = 't';
	actual = file::readall(WRITABLE_FILE);
	assert(actual.ptr());
	assert_eq(actual.size(), 8);
	assert(!memcmp(actual.ptr(), expected, 8));
	
	//test the various creation modes
	//file exists, these three should work
	assert( (f.open(WRITABLE_FILE, file::m_write)));
	assert_eq(f.size(), 8);
	assert( (f.open(WRITABLE_FILE, file::m_wr_existing)));
	assert_eq(f.size(), 8);
	assert( (f.open(WRITABLE_FILE, file::m_replace)));
	assert_eq(f.size(), 0);
	assert(!(f.open(WRITABLE_FILE, file::m_create_excl))); // but this should fail
	
	assert(file::unlink(WRITABLE_FILE));
	assert(!f.open(WRITABLE_FILE, file::m_wr_existing)); // this should fail
	assert(f.open(WRITABLE_FILE, file::m_create_excl)); // this should create
	assert(file::unlink(WRITABLE_FILE));
	
	assert(f.open(WRITABLE_FILE, file::m_replace)); // replacing a nonexistent file is fine
	//opening a nonexistent file with m_write is tested at the start of this function
	f.close();
	assert(file::unlink(WRITABLE_FILE));
	assert(file::unlink(WRITABLE_FILE)); // ensure it properly deals with unlinking a nonexistent file
}

test("in-memory files", "array", "file")
{
	array<byte> bytes;
	bytes.resize(8);
	for (int i=0;i<8;i++) bytes[i]=i;
	array<byte> bytes2;
	bytes2.resize(4);
	
	file f = file::mem(bytes.slice(0, 8));
	assert(f);
	assert_eq(f.size(), 8);
	assert_eq(f.pread(bytes2, 1), 4);
	for (int i=0;i<4;i++) assert_eq(bytes2[i], i+1);
	
	//readonly
	assert(!f.pwrite(bytes2, 6));
	assert(!f.replace(bytes2));
	assert(!f.mmapw());
	
	f = file::mem(bytes);
	assert(f);
	assert_eq(f.size(), 8);
	
	assert(f.pwrite(bytes2, 6));
	assert_eq(f.size(), 10);
	for (int i=0;i<6;i++) assert_eq(bytes[i], i);
	for (int i=0;i<4;i++) assert_eq(bytes[i+6], i+1);
	
	assert(f.replace(bytes2));
	for (int i=0;i<4;i++) assert_eq(bytes[i], i+1);
	assert_eq(f.size(), 4);
}

test("file::resolve", "array,string", "")
{
	assert_eq(file::resolve("foo/bar/../baz"), "foo/baz");
	assert_eq(file::resolve("./foo.txt"), "foo.txt");
	assert_eq(file::resolve("."), ".");
	assert_eq(file::resolve("../foo.txt"), "../foo.txt");
	assert_eq(file::resolve(".."), "..");
	assert_eq(file::resolve("foo//bar.txt"), "foo/bar.txt");
	assert_eq(file::resolve("/foo.txt"), "/foo.txt");
	assert_eq(file::resolve("//foo.txt"), "//foo.txt");
}
#endif
