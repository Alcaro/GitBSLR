#pragma once
#include "global.h"
#include <new>
#include <string.h>
#include <type_traits>
#include "linqbase.h"

template<typename T> class arrayview;
template<typename T> class arrayvieww;
template<typename T> class array;

//Do not remove the debug_or_print calls without testing both the Russian, the Walrus and the sandbox.
//Additionally, to ensure high chance I'll hit anything I don't remember that uses Arlib, do not remove until March 1, 2018.
//Same applies to similar constructions in set.h and string.h.
#include"os.h"

//size: two pointers
//this object does not own its storage, it's just a pointer wrapper
template<typename T> class arrayview : public linqbase<arrayview<T>> {
protected:
	T * items; // not const, despite not necessarily being writable; this makes arrayvieww/array a lot simpler
	size_t count;
	
protected:
	static const bool trivial_cons = std::is_trivial<T>::value; // constructor is memset(0)
#if __GNUC__ >= 5
	static const bool trivial_copy = std::is_trivially_copyable<T>::value; // copy constructor is memcpy
#else
	static const bool trivial_copy = trivial_cons;
#endif
	//static const bool trivial_comp = std::has_unique_object_representations<T>::value;
	static const bool trivial_comp = std::is_integral<T>::value; // equality comparison is memcmp
	//don't care about destructor being trivial
	
public:
	const T& operator[](size_t n) const { return items[n]; }
	
	const T* ptr() const { return items; }
	size_t size() const { return count; }
	
	explicit operator bool() const { return count; }
	
	arrayview()
	{
		this->items=NULL;
		this->count=0;
	}
	
	arrayview(null_t)
	{
		this->items=NULL;
		this->count=0;
	}
	
	arrayview(const T * ptr, size_t count)
	{
		this->items = (T*)ptr;
		this->count = count;
	}
	
	template<size_t N> arrayview(const T (&ptr)[N])
	{
		this->items = (T*)ptr;
		this->count = N;
	}
	
	arrayview<T> slice(size_t first, size_t count) { return arrayview<T>(this->items+first, count); }
	arrayview<T> skip(size_t n) { return slice(n, this->count-n); }
	
	T join() const
	{
		if (!this->count) return T();
		
		T out = this->items[0];
		for (size_t n=1;n<this->count;n++)
		{
			out += this->items[n];
		}
		return out;
	}
	
	template<typename T2> decltype(T() + T2()) join(T2 between) const
	{
		if (!this->count) return decltype(T() + T2())();
		
		decltype(T() + T2()) out = this->items[0];
		for (size_t n=1;n < this->count;n++)
		{
			out += between;
			out += this->items[n];
		}
		return out;
	}
	
	//WARNING: Keep track of endianness if using this.
	template<typename T2> arrayview<T2> reinterpret() const
	{
		//reject cast<string>()
		//TODO: allow litend/etc
		static_assert(std::is_fundamental<T>::value);
		static_assert(std::is_fundamental<T2>::value);
		
		size_t newsize = this->count*sizeof(T)/sizeof(T2);
		return arrayview<T2>((T2*)this->items, newsize);
	}
	
	template<typename T2> inline array<T2> cast() const;
	
	size_t find(const T& item) const
	{
		for (size_t n=0;n<this->count;n++)
		{
			if (this->items[n] == item) return n;
		}
		return -1;
	}
	bool contains(const T& item) const
	{
		return find(item) != (size_t)-1;
	}
	
	//arrayview(const arrayview<T>& other)
	//{
	//	clone(other);
	//}
	
	//arrayview<T> operator=(const arrayview<T>& other)
	//{
	//	clone(other);
	//	return *this;
	//}
	
	bool operator==(arrayview<T> other) const
	{
		if (size() != other.size()) return false;
		if (this->trivial_comp)
		{
			return memcmp(ptr(), other.ptr(), sizeof(T)*size())==0;
		}
		else
		{
			for (size_t i=0;i<size();i++)
			{
				if (!(items[i]==other[i])) return false;
			}
			return true;
		}
	}
	
	bool operator!=(arrayview<T> other) const
	{
		return !(*this == other);
	}
	
	const T* begin() const { return this->items; }
	const T* end() const { return this->items+this->count; }
};

//size: two pointers
//this one can write its storage, but doesn't own the storage itself
template<typename T> class arrayvieww : public arrayview<T> {
	//T * items;
	//size_t count;
public:
	
	T& operator[](size_t n) { return this->items[n]; }
	const T& operator[](size_t n) const { return this->items[n]; }
	
	T* ptr() { return this->items; }
	const T* ptr() const { return this->items; }
	
	arrayvieww()
	{
		this->items=NULL;
		this->count=0;
	}
	
	arrayvieww(null_t)
	{
		this->items=NULL;
		this->count=0;
	}
	
	arrayvieww(T * ptr, size_t count)
	{
		this->items = ptr;
		this->count = count;
	}
	
	template<size_t N> arrayvieww(T (&ptr)[N])
	{
		this->items = ptr;
		this->count = N;
	}
	
	arrayvieww(const arrayvieww<T>& other)
	{
		this->items = other.items;
		this->count = other.count;
	}
	
	arrayvieww<T> operator=(arrayvieww<T> other)
	{
		this->items = other.items;
		this->count = other.count;
		return *this;
	}
	
	arrayvieww<T> slice(size_t first, size_t count) { return arrayvieww<T>(this->items+first, count); }
	arrayvieww<T> skip(size_t n) { return slice(n, this->count-n); }
	
	//stable sort
	void sort()
	{
		//insertion sort, without binary search optimization for finding the new position
		for (size_t a=0;a<this->count;a++)
		{
			size_t b;
			for (b=0;b<a;b++)
			{
				if (this->items[a] < this->items[b]) break;
			}
			if (a == b) continue;
			
			char tmp[sizeof(T)];
			memcpy(tmp, this->items+a, sizeof(T));
			memmove(this->items+b+1, this->items+b, sizeof(T)*(a-b));
			memcpy(this->items+b, tmp, sizeof(T));
		}
	}
	
	//unstable sort, not necessarily quicksort
	void qsort()
	{
		sort(); // TODO: less lazy
	}
	
	//void shuffle()
	//{
	//	for (int i=count;i>0;i--)
	//	{
	//		swap(i-1, rand()%i);
	//	}
	//}
	
	const T* begin() const { return this->items; }
	const T* end() const { return this->items+this->count; }
	T* begin() { return this->items; }
	T* end() { return this->items+this->count; }
};

//size: two pointers, plus one T per item
//this one owns its storage and manages its memory
template<typename T> class array : public arrayvieww<T> {
	//T * items;
	//size_t count;
	
	void clone(const arrayview<T>& other)
	{
		this->count = other.size(); // I can't access non-this instances of my base class, so let's just use the public interface.
		this->items = malloc(sizeof(T)*bitround(this->count));
		if (this->trivial_copy)
		{
			memcpy(this->items, other.ptr(), sizeof(T)*this->count);
		}
		else
		{
			for (size_t i=0;i<this->count;i++)
			{
				new(&this->items[i]) T(other.ptr()[i]);
			}
		}
	}
	
public:
	void swap(array<T>& other)
	{
		T* newitems = other.items;
		size_t newcount = other.count;
		other.items = this->items;
		other.count = this->count;
		this->items = newitems;
		this->count = newcount;
	}
	
private:
	void resize_grow_noinit(size_t count)
	{
		if (this->count >= count) return;
		size_t bufsize_pre = bitround(this->count);
		size_t bufsize_post = bitround(count);
		if (bufsize_pre != bufsize_post) this->items = realloc(this->items, sizeof(T)*bufsize_post);
		this->count = count;
	}
	
	//it would be better if this thing didn't reallocate until it's a quarter of the original size
	//but I don't store the allocated size, so that's hard
	//there is malloc_usable_size (and similar), but it may or may not exist depending on the libc used
	void resize_shrink_noinit(size_t count)
	{
		if (this->count <= count) return;
		size_t bufsize_pre = bitround(this->count);
		size_t bufsize_post = bitround(count);
		if (bufsize_pre != bufsize_post) this->items = realloc(this->items, sizeof(T)*bufsize_post);
		this->count = count;
	}
	
	void resize_grow(size_t count)
	{
		if (this->count >= count) return;
		size_t prevcount = this->count;
		resize_grow_noinit(count);
		if (this->trivial_cons)
		{
			memset(this->items+prevcount, 0, sizeof(T)*(count-prevcount));
		}
		else
		{
			for (size_t i=prevcount;i<count;i++)
			{
				new(&this->items[i]) T();
			}
		}
	}
	
	void resize_shrink(size_t count)
	{
		if (this->count <= count) return;
		for (size_t i=count;i<this->count;i++)
		{
			this->items[i].~T();
		}
		resize_shrink_noinit(count);
	}
	
	void resize_to(size_t count)
	{
		if (count > this->count) resize_grow(count);
		else resize_shrink(count);
	}
	
public:
#ifdef MARCH2018
	T& operator[](size_t n) { return this->items[n]; }
#else
	T& operator[](size_t n) { if (n >= this->size()) { debug_or_print(); resize_grow(n+1); } return this->items[n]; }
#endif
	const T& operator[](size_t n) const { if (n >= this->size()) debug_or_print(); return this->items[n]; }
	
	void resize(size_t len) { resize_to(len); }
	void reserve(size_t len) { resize_grow(len); }
	void reserve_noinit(size_t len)
	{
		if (this->trivial_cons) resize_grow_noinit(len);
		else resize_grow(len);
	}
	
	T& insert(size_t index, T&& item)
	{
		resize_grow_noinit(this->count+1);
		memmove(this->items+index+1, this->items+index, sizeof(T)*(this->count-1-index));
		new(&this->items[index]) T(std::move(item));
		return this->items[index];
	}
	T& insert(size_t index, const T& item)
	{
		resize_grow_noinit(this->count+1);
		memmove(this->items+index+1, this->items+index, sizeof(T)*(this->count-1-index));
		new(&this->items[index]) T(item);
		return this->items[index];
	}
	T& insert(size_t index)
	{
		resize_grow_noinit(this->count+1);
		memmove(this->items+index+1, this->items+index, sizeof(T)*(this->count-1-index));
		new(&this->items[index]) T();
		return this->items[index];
	}
	
	void append(const arrayview<T>& item) = delete; // use += instead
	T& append(T&& item) { return insert(this->count, std::move(item)); }
	T& append(const T& item) { return insert(this->count, item); }
	T& append() { return insert(this->count); }
	T& prepend(T&& item) { return insert(0, std::move(item)); }
	T& prepend(const T& item) { return insert(0, item); }
	T& prepend() { return insert(0); }
	void reset() { resize_shrink(0); }
	
	void remove(size_t index)
	{
		this->items[index].~T();
		memmove(this->items+index, this->items+index+1, sizeof(T)*(this->count-1-index));
		resize_shrink_noinit(this->count-1);
	}
	
	T pop(size_t index = 0)
	{
		T ret(std::move(this->items[index]));
		remove(index);
		return std::move(ret);
	}
	
	array()
	{
		this->items = NULL;
		this->count = 0;
	}
	
	array(null_t)
	{
		this->items = NULL;
		this->count = 0;
	}
	
	array(const array<T>& other)
	{
		clone(other);
	}
	
	array(const arrayview<T>& other)
	{
		clone(other);
	}
	
	array(array<T>&& other)
	{
		swap(other);
	}
	
	array(std::initializer_list<T> c)
	{
		clone(arrayview<T>(c.begin(), c.size()));
	}
	
	array(const T * ptr, size_t count)
	{
		clone(arrayview<T>(ptr, count));
	}
	
	array<T> operator=(array<T> other)
	{
		swap(other);
		return *this;
	}
	
	array<T> operator=(arrayview<T> other)
	{
		if (other.ptr() >= this->ptr() && other.ptr() < this->ptr()+this->size())
		{
			size_t start = other.ptr()-this->ptr();
			size_t len = other.size();
			
			for (size_t i=0;i<start;i++) this->items[i].~T();
			memmove(this->ptr(), this->ptr()+start, sizeof(T)*len);
			for (size_t i=start+len;i<this->count;i++) this->items[i].~T();
			
			resize_shrink_noinit(len);
		}
		else
		{
			for (size_t i=0;i<this->count;i++) this->items[i].~T();
			free(this->items);
			clone(other);
		}
		return *this;
	}
	
	array<T>& operator+=(arrayview<T> other)
	{
		size_t prevcount = this->count;
		size_t othercount = other.size();
		
		const T* src;
		T* dst;
		
		if (other.ptr() >= this->ptr() && other.ptr() < this->ptr()+this->size())
		{
			size_t start = other.ptr()-this->ptr();
			
			resize_grow_noinit(prevcount + othercount);
			src = this->items+start;
			dst = this->items+prevcount;
		}
		else
		{
			resize_grow_noinit(prevcount + othercount);
			src = other.ptr();
			dst = this->items+prevcount;
		}
		
		if (this->trivial_copy)
		{
			memcpy(dst, src, sizeof(T)*othercount);
		}
		else
		{
			for (size_t i=0;i<othercount;i++)
			{
				new(&dst[i]) T(src[i]);
			}
		}
		
		return *this;
	}
	
	~array()
	{
		for (size_t i=0;i<this->count;i++) this->items[i].~T();
		free(this->items);
	}
	
	//takes ownership of the given data
	static array<T> create_usurp(arrayvieww<T> data)
	{
		array<T> ret;
		ret.items = data.items;
		ret.count = 0;
		ret.resize_grow_noinit(data.count);
		return ret;
	}
	
	//remember to call all applicable destructors if using this
	arrayvieww<T> release()
	{
		arrayvieww<T> ret = *this;
		this->items = NULL;
		this->count = 0;
		return ret;
	}
};

template<typename T> template<typename T2>
inline array<T2> arrayview<T>::cast() const
{
	array<T2> ret;
	for (const T& tmp : *this) ret.append(tmp);
	return std::move(ret);
}


template<> class array<bool> {
protected:
	static const size_t n_inline = sizeof(uint8_t*)/sizeof(uint8_t)*8;
	
	union {
		uint8_t bits_inline[n_inline/8];
		uint8_t* bits_outline;
	};
	size_t nbits;
	
	uint8_t* bits()
	{
		if (nbits <= n_inline) return bits_inline;
		else return bits_outline;
	}
	const uint8_t* bits() const
	{
		if (nbits <= n_inline) return bits_inline;
		else return bits_outline;
	}
	
	class entry {
		array<bool>& parent;
		size_t index;
		
	public:
		operator bool() const { return parent.get(index); }
		entry& operator=(bool val) { parent.set(index, val); return *this; }
		
		entry(array<bool>& parent, size_t index) : parent(parent), index(index) {}
	};
	friend class entry;
	
	bool get(size_t n) const
	{
		if (n >= nbits) debug_or_print();
		if (n >= nbits) return false;
		return bits()[n/8]>>(n&7) & 1;
	}
	
	void set(size_t n, bool val)
	{
		if (n >= nbits)
		{
			debug_or_print();
			resize(n+1);
		}
		uint8_t& byte = bits()[n/8];
		byte &=~ (1<<(n&7));
		byte |= (val<<(n&7));
	}
	
	//does not resize
	void set_slice(size_t start, size_t num, const array<bool>& other, size_t other_start)
	{
		if (&other == this)
		{
			//TODO: optimize
			array<bool> copy = other;
			set_slice(start, num, copy, other_start);
			return;
		}
		//TODO: optimize
		for (size_t i=0;i<num;i++)
		{
			set(start+i, other.get(other_start+i));
		}
	}
	
	void clear_unused(size_t nbytes)
	{
		size_t low = this->nbits;
		size_t high = nbytes*8;
		
		//TODO: figure out what purpose this could've possibly served, given that nbytes is a multiple of 8
		//high = (high+7)&~7; // wipe the rest of the byte, it doesn't matter
		
		if (low == high) return; // don't wipe bits()[8] if they're both 64
		
		uint8_t& byte = bits()[low/8];
		byte &=~ (0xFF<<(low&7));
		low = (low+7)&~7;
		
		memset(bits()+low/8, 0, (high-low)/8);
	}
	
	size_t alloc_size(size_t len)
	{
		return bitround((len+7)/8);
	}
	
public:
	bool operator[](size_t n) const { if (n >= size()) debug_or_print(); return get(n); }
	entry operator[](size_t n) { if (n >= size()) debug_or_print(); return entry(*this, n); }
	
	bool get_or(size_t n, bool def) const
	{
		if (n >= size()) return def;
		return get(n);
	}
	void set_resize(size_t n, bool val)
	{
		if (n >= size()) resize(n+1);
		set(n, val);
	}
	
	size_t size() const { return nbits; }
	void reset()
	{
		destruct();
		construct();
	}
	
	void resize(size_t len)
	{
		size_t prevlen = this->nbits;
		this->nbits = len;
		
		switch ((prevlen > n_inline)<<1 | (len > n_inline))
		{
		case 0: // small->small
			if (len < prevlen) clear_unused(sizeof(this->bits_inline));
			break;
		case 1: // small->big
			{
				size_t newbytes = alloc_size(len);
				uint8_t* newbits = malloc(newbytes);
				memcpy(newbits, this->bits_inline, sizeof(this->bits_inline));
				memset(newbits+sizeof(this->bits_inline), 0, newbytes-sizeof(this->bits_inline));
				bits_outline = newbits;
			}
			break;
		case 2: // big->small
			{
				uint8_t* freethis = this->bits_outline;
				memcpy(this->bits_inline, this->bits_outline, sizeof(this->bits_inline));
				free(freethis);
				clear_unused(sizeof(this->bits_inline));
			}
			break;
		case 3: // big->big
			{
				size_t prevbytes = alloc_size(prevlen);
				size_t newbytes = alloc_size(len);
				if (newbytes > prevbytes)
				{
					bits_outline = realloc(this->bits_outline, newbytes);
					clear_unused(newbytes);
				}
				if (len < prevlen)
				{
					clear_unused(newbytes);
				}
			}
			break;
		}
		
//printf("%lu->%lu t=%i", prevlen, len, ((prevlen > n_inline)<<1 | (len > n_inline)));
//size_t bytes;
//if (len > n_inline) bytes = alloc_size(len);
//else bytes = sizeof(bits_inline);
//for (size_t i=0;i<bitround(bytes);i++)
//{
//printf(" %.2X", bits()[i]);
//}
//puts("");
//bool fail=false;
//if (len>prevlen)
//{
//for (size_t n=prevlen;n<len;n++)
//{
//	if (get(n))
//	{
//		printf("%lu->%lu: unexpected at %lu\n", prevlen, len, n);
//		fail=true;
//	}
//}
//}
//if(fail)abort();
	}
	
	void append(bool item)
	{
		resize(this->nbits+1);
		set(this->nbits-1, item);
	}
	
	array<bool> slice(size_t first, size_t count) const
	{
		if ((first&7) == 0)
		{
			array<bool> ret;
			ret.resize(count);
			memcpy(ret.bits(), this->bits() + first/8, (count+7)/8);
			return ret;
		}
		else
		{
			//TODO: optimize
			array<bool> ret;
			ret.resize(count);
			for (size_t i=0;i<count;i++) ret.set(i, this->get(first+i));
			return ret;
		}
	}
	
private:
	void destruct()
	{
		if (nbits > n_inline) free(bits_outline);
	}
	void construct()
	{
		this->nbits = 0;
		memset(bits_inline, 0, sizeof(bits_inline));
	}
	void construct(const array& other)
	{
		nbits = other.nbits;
		if (nbits > n_inline)
		{
			size_t nbytes = alloc_size(nbits);
			bits_outline = malloc(nbytes);
			memcpy(bits_outline, other.bits_outline, nbytes);
		}
		else
		{
			memcpy(bits_inline, other.bits_inline, sizeof(bits_inline));
		}
	}
	void construct(array&& other)
	{
		memcpy(this, &other, sizeof(*this));
		other.nbits = 0;
	}
public:
	
	array() { construct(); }
	array(const array& other) { construct(other); }
	array(array&& other) { construct(other); }
	~array() { destruct(); }
	
	array& operator=(const array& other)
	{
		destruct();
		construct(other);
		return *this;
	}
	
	array& operator=(array&& other)
	{
		destruct();
		construct(other);
		return *this;
	}
	
	explicit operator bool() { return size(); }
	
	//missing maybe-useful features from the normal array:
	
	//array<bool> skip(size_t n) { return slice(n, size()-n); }
	//bool contains(bool item) const
	//bool operator==(array other) const
	//bool operator!=(array other) const
	//void swap(array& other)
	//void insert(size_t index, bool item)
	//void prepend(bool item) { insert(0, item); }
	//void remove(size_t index)
	//array& operator+=(const array<bool>& other)
	//const T* begin() { return this->items; }
	//const T* end() { return this->items+this->count; }
	//array(null_t)
};


//A refarray acts mostly like a normal array. The difference is that it stores pointers rather than the elements themselves;
//as such, you can't cast to arrayview or pointer, but you can keep pointers or references to the elements, or insert something virtual.
template<typename T> class refarray {
	array<autoptr<T>> items;
public:
	T& operator[](size_t n)
	{
		if (n >= items.size()) debug_or_print();
		return *items[n];
	}
	T& append()
	{
		T* ret = new T();
		items.append(ret);
		return *ret;
	}
	void append_take(T& item)
	{
		items.append(&item);
	}
	void remove(size_t index) { items.remove(index); }
	size_t size() { return items.size(); }
	
private:
	class enumerator {
		autoptr<T>* ptr;
	public:
		enumerator(autoptr<T>* ptr) : ptr(ptr) {}
		
		T& operator*() { return **ptr; }
		enumerator& operator++() { ++ptr; return *this; }
		bool operator!=(const enumerator& other) { return ptr != other.ptr; }
	};
public:
	enumerator begin() { return enumerator(items.ptr()); }
	enumerator end() { return enumerator(items.ptr() + items.size()); }
};
