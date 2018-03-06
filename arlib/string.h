#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include <string.h>
#include <ctype.h>

//A string is a mutable sequence of bytes. It usually represents UTF-8 text, but can be arbitrary binary data, including NULs.
//All string:: functions taking or returning a char* assume/guarantee NUL termination. However, anything using uint8_t* does not.

//cstring is an immutable sequence of bytes that does not own its storage. It can also be called stringview.

//If the string contains no NULs (not even at the end), it's considered 'weak proper'.
//If the string contains no control characters other than \t\r\n, and is valid UTF-8, it's considered 'proper'.
//Many string users expect some level of properity.


class string;

class cstring {
	friend class string;
	
	static const int obj_size = 16; // maximum 120, or the inline length overflows
	                                // (127 would fit, but that requires an extra alignment byte, which throws the sizeof assert)
	                                // minimum 16 (pointer + various members + alignment)
	                                //  (actually minimum 12 on 32bit, but who needs 32bit)
	static const int max_inline = obj_size-1;
	
	union {
		struct {
			uint8_t m_inline[max_inline];
			
			//this is how many bytes are unused by the raw string data
			//if all bytes are used, there are zero unused bytes - which also serves as the NUL
			//if not inlined, it's -1
			uint8_t m_inline_len;
		};
		struct {
			uint8_t* m_data; // if owning, there's also a int refcount before this pointer; if not owning, no such thing
			uint32_t m_len;
			bool m_nul; // whether the string is properly terminated (always true if owning)
			uint8_t reserved; // matches the last byte of the inline data; never ever access this
		};
	};
	
	bool inlined() const
	{
		static_assert(sizeof(cstring)==obj_size);
		
		return m_inline_len != (uint8_t)-1;
	}
	
	const uint8_t * ptr() const
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
public:
	uint32_t length() const
	{
		if (inlined()) return max_inline-m_inline_len;
		else return m_len;
	}
	
	arrayview<byte> bytes() const
	{
		return arrayview<byte>(ptr(), length());
	}
	//If this is true, bytes()[bytes().length()] is '\0'. If false, it's undefined behavior.
	bool bytes_hasterm() const
	{
		return (inlined() || m_nul);
	}
	
private:
	void init_from_nocopy(const char * str)
	{
		if (!str) str = "";
		init_from_nocopy(arrayview<byte>((uint8_t*)str, strlen(str)));
		if (!inlined()) m_nul = true;
	}
	void init_from_nocopy(arrayview<byte> data)
	{
		const uint8_t * str = data.ptr();
		uint32_t len = data.size();
		
		if (len <= max_inline)
		{
			memcpy(m_inline, str, len);
			m_inline[len] = '\0';
			m_inline_len = max_inline-len;
		}
		else
		{
			m_inline_len = -1;
			
			m_data = (uint8_t*)str;
			m_len = len;
			m_nul = false;
		}
	}
	void init_from_nocopy(const cstring& other)
	{
		memcpy(this, &other, sizeof(*this));
	}
	
	//~0 means end of the string, ~1 is last character
	//don't try to make -1 the last character, it makes str.substr(x, ~0) blow up
	int32_t realpos(int32_t pos) const
	{
		if (pos >= 0) return pos;
		else return length()-~pos;
	}
	
	char getchar(int32_t index) const
	{
		//this function is REALLY hot, use the strongest possible optimizations
		if (index >= 0)
		{
			if ((uint32_t)index > length()) debug_or_print();
			
			if (inlined()) return m_inline[index];
			else if ((uint32_t)index < m_len) return m_data[index];
			else return '\0';
		}
		
		debug_or_print();
		return getchar(realpos(index));
	}
	
public:
	string replace(cstring in, cstring out);
	
private:
	class noinit {};
	cstring(noinit) {}
	
public:
	cstring() { init_from_nocopy(""); }
	cstring(const cstring& other) { init_from_nocopy(other); }
	cstring(const char * str) { init_from_nocopy(str); }
	//cstring(const uint8_t * str, uint32_t len) { init_from(str, len); }
	cstring(arrayview<uint8_t> bytes) { init_from_nocopy(bytes); }
	cstring(arrayview<char> chars) { init_from_nocopy(chars.reinterpret<uint8_t>()); }
	cstring(nullptr_t) { init_from_nocopy(""); }
	cstring& operator=(const cstring& other) { init_from_nocopy(other); return *this; }
	cstring& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstring& operator=(nullptr_t) { init_from_nocopy(""); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	//operator const char * () const { return ptr_withnul(); }
	
	char operator[](int index) const { if (index < 0) debug_or_print(); return getchar(index); }
	
	//static string create(arrayview<uint8_t> data) { string ret=noinit(); ret.init_from(data.ptr(), data.size()); return ret; }
	
	cstring substr(int32_t start, int32_t end) const
	{
		start = realpos(start);
		end = realpos(end);
		return cstring(arrayview<byte>(ptr()+start, end-start));
	}
	
	bool contains(cstring other) const
	{
		return memmem(this->ptr(), this->length(), other.ptr(), other.length()) != NULL;
	}
	size_t indexof(cstring other) const
	{
		uint8_t* ptr = (uint8_t*)memmem(this->ptr(), this->length(), other.ptr(), other.length());
		if (ptr) return ptr - this->ptr();
		else return (size_t)-1;
	}
	bool startswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		return (!memcmp(this->ptr(), other.ptr(), other.length()));
	}
	bool endswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		return (!memcmp(this->ptr()+this->length()-other.length(), other.ptr(), other.length()));
	}
	bool istartswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		const char* a = (char*)this->ptr();
		const char* b = (char*)other.ptr();
		for (size_t i=0;i<other.length();i++)
		{
			if (tolower(a[i]) != tolower(b[i])) return false;
		}
		return true;
	}
	bool iendswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		const char* a = (char*)this->ptr()+this->length()-other.length();
		const char* b = (char*)other.ptr();
		for (size_t i=0;i<other.length();i++)
		{
			if (tolower(a[i]) != tolower(b[i])) return false;
		}
		return true;
	}
	bool iequals(cstring other) const
	{
		return (this->length() == other.length() && this->istartswith(other));
	}
	
	array<cstring> csplit(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> csplit(cstring sep) const { return csplit(sep, limit); }
	
	array<cstring> crsplit(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> crsplit(cstring sep) const { return crsplit(sep, limit); }
	
	array<string> split(cstring sep, size_t limit) const { return csplit(sep, limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> split(cstring sep) const { return split(sep, limit); }
	
	array<string> rsplit(cstring sep, size_t limit) const { return crsplit(sep, limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> rsplit(cstring sep) const { return rsplit(sep, limit); }
	
	array<cstring> csplitw(size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> csplitw() const { return csplitw(limit); }
	
	array<cstring> crsplitw(size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> crsplitw() const { return crsplitw(limit); }
	
	array<string> splitw(size_t limit) const { return csplitw(limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> splitw() const { return splitw(limit); }
	
	array<string> rsplitw(size_t limit) const { return crsplitw(limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> rsplitw() const { return rsplitw(limit); }
	
	cstring trim() const
	{
		const uint8_t * chars = ptr();
		int start = 0;
		int end = length();
		while (end > start && isspace(chars[end-1])) end--;
		while (start < end && isspace(chars[start])) start++;
		return substr(start, end);
	}
	
	inline string lower() const;
	inline string upper() const;
	string fromlatin1() const;
	string fromwindows1252() const;
	
	size_t hash() const { return ::hash((char*)ptr(), length()); }
	
private:
	class c_string {
		char* ptr;
		bool do_free;
	public:
		
		c_string(arrayview<byte> data, bool has_term)
		{
			if (has_term)
			{
				ptr = (char*)data.ptr();
				do_free = false;
			}
			else
			{
				ptr = (char*)malloc(data.size()+1);
				memcpy(ptr, data.ptr(), data.size());
				ptr[data.size()] = '\0';
				do_free = true;
			}
		}
		operator const char *() const { return ptr; }
		~c_string() { if (do_free) free(ptr); }
	};
public:
	//no operator const char *, a cstring doesn't necessarily have a NUL terminator
	c_string c_str() const { return c_string(bytes(), bytes_hasterm()); }
};


class string : public cstring {
	friend class cstring;
	
	static size_t bytes_for(uint32_t len)
	{
		return bitround(len+1);
	}
	//static uint8_t* alloc(uint8_t* prev, uint32_t prevsize, uint32_t newsize);
	
	uint8_t * ptr()
	{
		return (uint8_t*)cstring::ptr();
	}
	const uint8_t * ptr() const
	{
		return (uint8_t*)cstring::ptr();
	}
	
	void resize(uint32_t newlen);
	
	const char * ptr_withnul() const { return (char*)ptr(); }
	
	void init_from(const char * str)
	{
		if (!str) str = "";
		init_from(arrayview<byte>((uint8_t*)str, strlen(str)));
	}
	void init_from(arrayview<byte> data);
	void init_from(cstring other)
	{
		init_from(other.bytes());
	}
	void init_from(string&& other)
	{
		memcpy(this, &other, sizeof(*this));
		other.m_inline_len = 0;
	}
	
	void release()
	{
		if (!inlined()) free(m_data);
	}
	
	void setchar(int32_t index_, char val)
	{
		uint32_t index = realpos(index_);
		if (index == length())
		{
			debug_or_print();
			resize(index+1);
		}
		ptr()[index] = val;
	}
	
	//TODO: arrayview
	void append(arrayview<uint8_t> newdat)
	{
		if (newdat.ptr() >= (uint8_t*)ptr() && newdat.ptr() < (uint8_t*)ptr()+length())
		{
			uint32_t offset = newdat.ptr() - ptr();
			uint32_t oldlength = length();
			resize(oldlength + newdat.size());
			memcpy(ptr() + oldlength, ptr() + offset, newdat.size());
		}
		else
		{
			uint32_t oldlength = length();
			resize(oldlength + newdat.size());
			memcpy(ptr() + oldlength, newdat.ptr(), newdat.size());
		}
	}
	
	void replace_set(int32_t pos, int32_t len, cstring newdat);
	
public:
	//Resizes the string to a suitable size, then allows the caller to fill it in with whatever. Initial contents are undefined.
	//The returned pointer may only be used until the first subsequent use of the string, including read-only operations.
	arrayvieww<byte> construct(uint32_t len)
	{
		resize(len);
		return arrayvieww<byte>(ptr(), len);
	}
	
	string& operator+=(const char * right)
	{
		append(arrayview<uint8_t>((uint8_t*)right, strlen(right)));
		return *this;
	}
	
	string& operator+=(cstring right)
	{
		append(right.bytes());
		return *this;
	}
	
	
	string& operator+=(char right)
	{
		uint8_t tmp = right;
		append(arrayview<uint8_t>(&tmp, 1));
		return *this;
	}
	
	string& operator+=(uint8_t right)
	{
		append(arrayview<uint8_t>(&right, 1));
		return *this;
	}
	
	// for other integer types, fail
	string& operator+=(int right) = delete;
	string& operator+=(unsigned right) = delete;
	
	
	string() : cstring(noinit()) { init_from(""); }
	string(const string& other) : cstring(noinit()) { init_from(other); }
	string(string&& other) : cstring(noinit()) { init_from(std::move(other)); }
	string(const char * str) : cstring(noinit()) { init_from(str); }
	string(cstring other) : cstring(noinit()) { init_from(other); }
	//string(const uint8_t * str, uint32_t len) { init_from(str, len); }
	string(arrayview<uint8_t> bytes) : cstring(noinit()) { init_from(bytes); }
	string(arrayview<char> chars) : cstring(noinit())
	{
		init_from(chars.reinterpret<uint8_t>());
	}
	string(nullptr_t) { init_from(""); }
	string& operator=(const string& other) { release(); init_from(other); return *this; }
	string& operator=(string&& other) { release(); init_from(std::move(other)); return *this; }
	string& operator=(const char * str) { release(); init_from(str); return *this; }
	string& operator=(nullptr_t) { release(); init_from(""); return *this; }
	~string() { release(); }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
private:
	class charref : nocopy {
		friend class string;
		string* parent;
		uint32_t index;
		charref(string* parent, uint32_t index) : parent(parent), index(index) {}
		
	public:
		charref& operator=(char ch) { parent->setchar(index, ch); return *this; }
		charref& operator+=(char ch) { parent->setchar(index, parent->getchar(index) + ch); return *this; }
		charref& operator-=(char ch) { parent->setchar(index, parent->getchar(index) - ch); return *this; }
		operator char() { return parent->getchar(index); }
	};
	friend class charref;
	
public:
	//Reading the NUL terminator is fine. Writing extends the string. Poking outside the string is undefined.
	//charref operator[](uint32_t index) { return charref(this, index); }
	charref operator[](int index) { if (index < 0) debug_or_print(); return charref(this, index); }
	//char operator[](uint32_t index) const { return getchar(index); }
	char operator[](int index) const { if (index < 0) debug_or_print(); return getchar(index); }
	
	static string create_usurp(char * str);
	
	//static string create(arrayview<uint8_t> data) { string ret=noinit(); ret.init_from(data.ptr(), data.size()); return ret; }
	
	/*
	string lower()
	{
		string ret = *this;
		ret.unshare();
		uint8_t * chars = ret.ptr();
		for (size_t i=0;i<length();i++) chars[i] = tolower(chars[i]);
		return ret;
	}
	
	string upper()
	{
		string ret = *this;
		ret.unshare();
		uint8_t * chars = ret.ptr();
		for (size_t i=0;i<length();i++) chars[i] = toupper(chars[i]);
		return ret;
	}
	*/
	
	static string codepoint(uint32_t cp);
private:
	static const uint16_t windows1252tab[32];
public:
	static uint32_t cpfromwindows1252(uint8_t byte)
	{
		if (byte >= 0x80 && byte <= 0x9F) return windows1252tab[byte-0x80];
		else return byte;
	}
};

inline bool operator==(cstring left,      const char * right) { return left.bytes() == arrayview<byte>((uint8_t*)right,strlen(right)); }
inline bool operator==(cstring left,      cstring right     ) { return left.bytes() == right.bytes(); }
inline bool operator==(const char * left, cstring right     ) { return operator==(right, left); }
inline bool operator!=(cstring left,      const char * right) { return !operator==(left, right); }
inline bool operator!=(cstring left,      cstring right     ) { return !operator==(left, right); }
inline bool operator!=(const char * left, cstring right     ) { return !operator==(left, right); }

inline string operator+(cstring left,      cstring right     ) { string ret=left; ret+=right; return ret; }
inline string operator+(string&& left,     const char * right) { left+=right; return left; }
inline string operator+(cstring left,      const char * right) { string ret=left; ret+=right; return ret; }
inline string operator+(string&& left,     cstring right     ) { left+=right; return left; }
inline string operator+(const char * left, cstring right     ) { string ret=left; ret+=right; return ret; }

inline string operator+(string&& left, char right) { left+=right; return left; }
inline string operator+(cstring left, char right) { string ret=left; ret+=right; return ret; }
inline string operator+(char left, cstring right) { string ret; ret[0]=left; ret+=right; return ret; }

inline string operator+(string&& left, int right) = delete;
inline string operator+(cstring left, int right) = delete;
inline string operator+(int left, cstring right) = delete;

inline string cstring::lower() const
{
	string ret = *this;
	uint8_t * chars = ret.ptr();
	for (size_t i=0;i<length();i++) chars[i] = tolower(chars[i]);
	return ret;
}

inline string cstring::upper() const
{
	string ret = *this;
	uint8_t * chars = ret.ptr();
	for (size_t i=0;i<length();i++) chars[i] = toupper(chars[i]);
	return ret;
}

//Checks if needle is one of the 'separator'-separated words in the haystack. The needle may not contain 'separator' or be empty.
//For example, haystack "GL_EXT_FOO GL_EXT_BAR GL_EXT_QUUX" (with space as separator) contains needles
// 'GL_EXT_FOO', 'GL_EXT_BAR' and 'GL_EXT_QUUX', but not 'GL_EXT_QUU'.
bool strtoken(const char * haystack, const char * needle, char separator);
