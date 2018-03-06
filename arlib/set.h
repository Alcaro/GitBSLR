#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include "linqbase.h"

template<typename T>
class set : public linqbase<set<T>> {
	//this is a hashtable, using open addressing and linear probing
	
	enum { i_empty, i_deleted };
	uint8_t& tag(size_t id) { return *(uint8_t*)(m_data+id); }
	uint8_t tag(size_t id) const { return *(uint8_t*)(m_data+id); }
	
	T* m_data; // length is always same as m_valid, so no explicit length - waste of space
	array<bool> m_valid;
	size_t m_count;
	
	void rehash(size_t newsize)
	{
//debug("rehash pre");
		T* prev_data = m_data;
		array<bool> prev_valid = std::move(m_valid);
		
		m_data = calloc(newsize, sizeof(T));
		m_valid.reset();
		m_valid.resize(newsize);
		
		for (size_t i=0;i<prev_valid.size();i++)
		{
			if (!prev_valid[i]) continue;
			
			size_t pos = find_pos_full<true, false>(prev_data[i]);
			//this is known to not overwrite any existing object; if it does, someone screwed up
			memcpy(&m_data[pos], &prev_data[i], sizeof(T));
			m_valid[pos] = true;
		}
		free(prev_data);
//debug("rehash post");
	}
	
	void grow()
	{
		// half full -> rehash
		if (m_count >= m_valid.size()/2) rehash(m_valid.size()*2);
	}
	
	bool slot_empty(size_t pos) const
	{
		if (pos >= m_valid.size())
		{
			printf("%i,%i\n",(int)pos,(int)m_valid.size());
			debug_or_print();
		}
		return !m_valid[pos];
	}
	
	//If the object exists, returns the index where it can be found.
	//If not, and want_empty is true, returns a suitable empty slot to insert it in, or -1 if the object should rehash.
	//If no such object and want_empty is false, returns -1.
	template<bool want_empty, bool want_used = true, typename T2>
	size_t find_pos_full(const T2& item) const
	{
		if (!m_data) return -1;
		
		size_t hashv = hash_shuffle(hash(item));
		size_t i = 0;
		
		size_t emptyslot = -1;
		
		while (true)
		{
			//I could use hashv + i+(i+1)/2 <http://stackoverflow.com/a/15770445>
			//but due to hash_shuffle, it hurts as much as it helps.
			size_t pos = (hashv + i) & (m_valid.size()-1);
			if (want_used && m_valid[pos] && m_data[pos] == item) return pos;
			if (!m_valid[pos])
			{
				if (emptyslot == (size_t)-1) emptyslot = pos;
				if (tag(pos) == i_empty)
				{
					if (want_empty) return emptyslot;
					else return -1;
				}
			}
			i++;
			if (i == m_valid.size())
			{
				//happens if all empty slots are i_deleted, no i_empty
				return -1;
			}
		}
	}
	
	template<typename T2>
	size_t find_pos_const(const T2& item) const
	{
		return find_pos_full<false>(item);
	}
	
	template<typename T2>
	size_t find_pos_insert(const T2& item)
	{
		size_t pos = find_pos_full<true>(item);
		if (pos == (size_t)-1)
		{
			rehash(m_valid.size());
			//after rehashing, there is always a suitable empty slot
			return find_pos_full<true, false>(item);
		}
		return pos;
	}
	
	template<typename,typename>
	friend class map;
	//used by map
	//if the item doesn't exist, NULL
	template<typename T2>
	T* get(const T2& item) const
	{
		size_t pos = find_pos_const(item);
		if (pos != (size_t)-1) return &m_data[pos];
		else return NULL;
	}
	//also used by map
	template<typename T2>
	T& get_create(const T2& item)
	{
		size_t pos = find_pos_insert(item);
		
		if (pos == (size_t)-1 || !m_valid[pos])
		{
			grow();
			pos = find_pos_insert(item); // recalculate this, grow() may have moved it
			//do not move grow() earlier, it invalidates references and get_create(item_that_exists) is not allowed to do that
			
			new(&m_data[pos]) T(item);
			m_valid[pos] = true;
			m_count++;
		}
		
		return m_data[pos];
	}
	
	void construct()
	{
		m_data = NULL;
		m_valid.resize(8);
		m_count = 0;
	}
	void construct(const set& other)
	{
		m_data = calloc(other.m_valid.size(), sizeof(T));
		m_valid = other.m_valid;
		m_count = other.m_count;
		
		for (size_t i=0;i<m_valid.size();i++)
		{
			if (m_valid[i])
			{
				new(&m_data[i]) T(other.m_data[i]);
			}
		}
	}
	void construct(set&& other)
	{
		m_data = std::move(other.m_data);
		m_valid = std::move(other.m_valid);
		m_count = std::move(other.m_count);
		
		other.construct();
	}
	
	void destruct()
	{
		for (size_t i=0;i<m_valid.size();i++)
		{
			if (m_valid[i])
			{
				m_data[i].~T();
			}
		}
		m_count = 0;
		free(m_data);
		m_valid.reset();
	}
	
public:
	set() { construct(); }
	set(const set& other) { construct(other); }
	set(set&& other) { construct(std::move(other)); }
	set(std::initializer_list<T> c)
	{
		construct();
		for (const T& item : c) add(item);
	}
	set& operator=(const set& other) { destruct(); construct(other); }
	set& operator=(set&& other) { destruct(); construct(std::move(other)); }
	~set() { destruct(); }
	
	template<typename T2>
	void add(const T2& item)
	{
		get_create(item);
	}
	
	template<typename T2>
	bool contains(const T2& item) const
	{
		size_t pos = find_pos_const(item);
		return pos != (size_t)-1;
	}
	
	template<typename T2>
	void remove(const T2& item)
	{
		size_t pos = find_pos_const(item);
		if (pos == (size_t)-1) return;
		
		m_data[pos].~T();
		tag(pos) = i_deleted;
		m_valid[pos] = false;
		m_count--;
		if (m_count < m_valid.size()/4 && m_valid.size() > 8) rehash(m_valid.size()/2);
	}
	
	size_t size() const { return m_count; }
	
	void reset() { destruct(); construct(); }
	
	class iterator {
		friend class set;
		
		const set* parent;
		size_t pos;
		
		void to_valid()
		{
			while (pos < parent->m_valid.size() && !parent->m_valid[pos]) pos++;
			if (pos == parent->m_valid.size()) pos = -1;
		}
		
		iterator(const set<T>* parent, size_t pos) : parent(parent), pos(pos)
		{
			if (pos != (size_t)-1) to_valid();
		}
		
	public:
		
		const T& operator*()
		{
			return parent->m_data[pos];
		}
		
		iterator& operator++()
		{
			pos++;
			to_valid();
			return *this;
		}
		
		bool operator!=(const iterator& other)
		{
			return (this->parent != other.parent || this->pos != other.pos);
		}
	};
	
	//messing with the set during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	//as such, 'for (T i : my_set) { my_set.remove(i); }' is safe (though may keep some instances)
	iterator begin() const { return iterator(this, 0); }
	iterator end() const { return iterator(this, -1); }

//string debug_node(int n) { return tostring(n); }
//string debug_node(string& n) { return n; }
//template<typename T2> string debug_node(T2& n) { return "?"; }
//void debug(const char * why)
//{
//puts("---");
//for (size_t i=0;i<m_data.size();i++)
//{
//	printf("%s %lu: valid %i, tag %i, data %s, found slot %lu\n",
//		why, i, (bool)m_valid[i], m_data[i].tag(), (const char*)debug_node(m_data[i].member()), find_pos(m_data[i].member()));
//}
//puts("---");
//}
};



template<typename Tkey, typename Tvalue>
class map : public linqbase<map<Tkey,Tvalue>> {
public:
	struct node {
		const Tkey key;
		Tvalue value;
		
		node() : key(), value() {}
		node(const Tkey& key) : key(key), value() {}
		node(const Tkey& key, const Tvalue& value) : key(key), value(value) {}
		//node(node other) : key(other.key), value(other.value) {}
		
		size_t hash() const { return ::hash(key); }
		bool operator==(const Tkey& other) { return key == other; }
		bool operator==(const node& other) { return key == other.key; }
	};
private:
	set<node> items;
	
public:
	//map() {}
	//map(const map& other) : items(other.items) {}
	//map(map&& other) : items(std::move(other.items)) {}
	//map& operator=(const map& other) { items = other.items; }
	//map& operator=(map&& other) { items = std::move(other.items); }
	//~map() { destruct(); }
	
	//can't call it set(), conflict with set<>
	void insert(const Tkey& key, const Tvalue& value)
	{
		items.add(node(key, value));
	}
	
	//if nonexistent, undefined behavior
	template<typename Tk2>
	Tvalue& get(const Tk2& key)
	{
		return items.get(key)->value;
	}
	
	//if nonexistent, returns 'def'
	template<typename Tk2>
	Tvalue& get_or(const Tk2& key, Tvalue& def)
	{
		node* ret = items.get(key);
		if (ret) return ret->value;
		else return def;
	}
	template<typename Tk2>
	Tvalue get_or(const Tk2& key, Tvalue def)
	{
		node* ret = items.get(key);
		if (ret) return ret->value;
		else return def;
	}
	template<typename Tk2>
	Tvalue* get_or_null(const Tk2& key)
	{
		node* ret = items.get(key);
		if (ret) return &ret->value;
		else return NULL;
	}
	Tvalue& get_create(const Tkey& key)
	{
		return items.get_create(key).value;
	}
	Tvalue& operator[](const Tkey& key) // C# does this better...
	{
		if (!contains(key)) debug_or_print();
		return get_create(key);
	}
	
	Tvalue& insert(const Tkey& key)
	{
		return get_create(key);
	}
	
	template<typename Tk2>
	bool contains(const Tk2& item) const
	{
		return items.contains(item);
	}
	
	template<typename Tk2>
	void remove(const Tk2& item)
	{
		items.remove(item);
	}
	
	void reset()
	{
		items.reset();
	}
	
	size_t size() const { return items.size(); }
	
private:
	class iterator {
		typename set<node>::iterator it;
	public:
		iterator(typename set<node>::iterator it) : it(it) {}
		
		node& operator*() { return const_cast<node&>(*it); }
		iterator& operator++() { ++it; return *this; }
		bool operator!=(const iterator& other) { return it != other.it; }
	};
	
	class c_iterator {
		typename set<node>::iterator it;
	public:
		c_iterator(typename set<node>::iterator it) : it(it) {}
		
		const node& operator*() { return const_cast<node&>(*it); }
		c_iterator& operator++() { ++it; return *this; }
		bool operator!=(const c_iterator& other) { return it != other.it; }
	};
	
public:
	//messing with the map during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	
	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	c_iterator begin() const { return items.begin(); }
	c_iterator end() const { return items.end(); }
	
	template<typename Ts>
	void serialize(Ts& s)
	{
		if (s.serializing)
		{
			for (node& p : *this)
			{
				s.item(tostring(p.key), p.value);
			}
		}
		else
		{
			Tkey tmpk;
			if (fromstring(s.next(), tmpk))
			{
				Tvalue& tmpv = items.get_create(tmpk).value;
				s.item(s.next(), tmpv);
			}
		}
	}
};
