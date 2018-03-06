#include "set.h"
#include "test.h"

#ifdef ARLIB_TEST
static int n_instancecount;
class instancecount {
public:
	instancecount() { n_instancecount++; }
	
	bool operator==(const instancecount& other) const { return true; }
	size_t hash() const { return 0; }
};

test("set", "array", "set")
{
	{
		set<int> item;
		uint32_t set_flags;
		
		assert(!item.contains(1));
		assert(!item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 0);
		
		item.add(1);
		assert(item.contains(1));
		assert(!item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1);
		
		item.add(2);
		assert(item.contains(1));
		assert(item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1 | 1<<2);
		
		item.remove(1);
		assert(!item.contains(1));
		assert(item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<2);
	}
	
	{
		set<string> item;
		
		assert(!item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.add("foo");
		assert(item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.add("bar");
		assert(item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.remove("foo");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.remove("baz");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
	}
	
	{
		for (int i=0;i<32;i++)
		{
			set<int> item;
			for (int j=0;j<i;j++)
			{
				for (int k=0;k<=j;k++)
				{
					item.add(j); // add the same thing multiple times for no reason
					assert(item.contains(j));
				}
				for (int k=0;k<=j;k++)
				{
					assert(item.contains(j));
				}
				assert_eq(item.size(), j+1);
			}
			
			
			for (int j=0;j<32;j++)
			{
				if (j<i) assert(item.contains(j));
				else assert(!item.contains(j));
			}
		}
	}
	
	{
		set<string> item;
		
		item.add("a");
		item.add("b");
		item.add("c");
		item.add("d");
		item.add("e");
		
		uint32_t set_flags = 0;
		size_t len_iter = 0;
		for (auto& x : item)
		{
			assert(x.length()==1 && x[0]>='a' && x[0]<='z');
			
			uint32_t flag = 1<<(x[0]-'a');
			assert_eq(set_flags&flag, 0);
			set_flags |= flag;
			
			len_iter++;
			assert(len_iter < 32);
		}
		assert_eq(len_iter, item.size());
		assert_eq(set_flags, (1<<item.size())-1);
	}
	
	{
		set<int> item;
		
		item.add(1);
		item.add(2);
		item.add(3);
		item.add(4);
		item.add(5);
		item.add(6);
		item.add(7);
		item.add(8);
		
		uint32_t set_flags = 0;
		size_t len_iter = 0;
		for (auto& x : item) // ensure this doesn't blow up if the last slot in the set is used
		{
			assert(x >= 0 && x < 32);
			
			uint32_t flag = 1 << x;
			assert_eq(set_flags&flag, 0);
			set_flags |= flag;
			
			len_iter++;
			assert(len_iter < 32);
		}
		assert_eq(len_iter, item.size());
		assert_eq(set_flags, ((1<<item.size())-1)<<1);
	}
	
	{
		class unhashable {
			int x;
		public:
			unhashable(int x) : x(x) {}
			size_t hash() const { return 0; }
			bool operator==(const unhashable& other) const { return x==other.x; }
		};
		
		set<unhashable> item;
		
		for (int i=0;i<256;i++) item.add(unhashable(i));
		
		//test passes if it does not enter an infinite loop of trying the same 4 slots over and
		// over, when the other 4 are unused
	}
	
	{
		set<int> item;
		
		for (int i=0;i<256;i++)
		{
			item.add(i);
			item.remove(i);
		}
		
		//test passes if it does not enter an infinite loop of looking for a 'nothing more to see
		// here' slot when all slots are 'there was something here, but keep looking'
	}
	
	{
		set<int> set1 = { 1, 2, 3 };
		set<int> set2 = std::move(set1);
		
		assert(set2.contains(1));
		assert(set2.contains(2));
		assert(set2.contains(3));
		
		assert(!set1.contains(1));
		assert(!set1.contains(2));
		assert(!set1.contains(3));
	}
	
	{
		n_instancecount = 0;
		
		set<instancecount> foo;
		assert_eq(n_instancecount, 0);
		instancecount n;
		assert_eq(n_instancecount, 1);
		foo.add(n);
		assert_eq(n_instancecount, 1);
	}
}

test("map", "array", "set")
{
	{
		map<int,int> item;
		uint32_t set_flags;
		
		assert(!item.contains(1));
		assert(!item.contains(2));
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 0);
		
		item.insert(1, 3);
		assert(item.contains(1));
		assert(!item.contains(2));
		assert_eq(item.get(1), 3);
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1);
		
		item.insert(2, 4);
		assert(item.contains(1));
		assert(item.contains(2));
		assert_eq(item.get(1), 3);
		assert_eq(item.get(2), 4);
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1 | 1<<2);
		
		item.remove(1);
		assert(!item.contains(1));
		assert(item.contains(2));
		assert_eq(item.get(2), 4);
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<2);
	}
	
	{
		map<string,string> item;
		
		assert(!item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.insert("foo", "Foo");
		assert(item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("foo"), "Foo");
		
		item.insert("bar", "Bar");
		assert(item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("foo"), "Foo");
		assert_eq(item.get("bar"), "Bar");
		
		item.remove("foo");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("bar"), "Bar");
		
		item.remove("baz");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("bar"), "Bar");
	}
	
	{
		map<int,int> map1;
		map1.insert(1,2);
		map1.insert(2,4);
		map1.insert(3,6);
		map<int,int> map2 = std::move(map1);
		
		assert(map2[1]==2);
		assert(map2[2]==4);
		assert(map2[3]==6);
		
		assert(!map1.contains(1));
		assert(!map1.contains(2));
		assert(!map1.contains(3));
	}
	
	{
		map<int,int> x;
		for (int i=0;i<256;i++)
		{
			x.insert(i, i*2 + 3);
			for (int j=0;j<i;j++)
			{
				assert_eq(x.get(j), j*2 + 3);
			}
		}
	}
	
	{
		map<int,int> x;
		for (int i=0;i<256;i++)
		{
			int& y = x.get_create(i);
			x.get_create(i); // make sure this does not invalidate existing references
			y=42;
		}
		assert_eq(x.get_or(256, 42), 42);
	}
	
	{
		n_instancecount = 0;
		
		map<instancecount,instancecount> foo;
		assert_eq(n_instancecount, 0);
		instancecount n1;
		instancecount n2;
		assert_eq(n_instancecount, 2);
		foo.insert(n1, n2);
		assert_eq(n_instancecount, 2);
	}
}
#endif
