#pragma once
#include "global.h"

template<typename T>
typename std::enable_if<std::is_integral<T>::value, size_t>::type hash(T val)
{
	return val;
}
template<typename T>
typename std::enable_if<std::is_class<T>::value, size_t>::type hash(const T& val)
{
	return val.hash();
}
static inline size_t hash(const char * val, size_t n)
{
	size_t hash = 5381;
	while (n--)
	{
		hash = hash*31 ^ *val;
		val++;
	}
	return hash;
}
static inline size_t hash(const char * val)
{
	return hash(val, strlen(val));
}


//implementation from https://stackoverflow.com/a/263416
inline size_t hashall() { return 2166136261; }
template<typename T, typename... Tnext> inline size_t hashall(T first, Tnext... next)
{
	size_t tail = hash(first);
	size_t heads = hashall(next...);
	return (heads*16777619) ^ tail;
}


//these two are reversible, but I never implemented the reversal because lazy.
inline uint32_t hash_shuffle(uint32_t val)
{
	//https://code.google.com/p/smhasher/wiki/MurmurHash3
	val ^= val >> 16;
	val *= 0x85ebca6b;
	val ^= val >> 13;
	val *= 0xc2b2ae35;
	val ^= val >> 16;
	return val;
}

inline uint64_t hash_shuffle(uint64_t val)
{
	//http://zimbry.blogspot.se/2011/09/better-bit-mixing-improving-on.html Mix13
	val ^= val >> 30;
	val *= 0xbf58476d1ce4e5b9;
	val ^= val >> 27;
	val *= 0x94d049bb133111eb;
	val ^= val >> 31;
	return val;
}
