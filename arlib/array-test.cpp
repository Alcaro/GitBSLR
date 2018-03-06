#include "array.h"
#include "test.h"

#ifdef ARLIB_TEST
test("array", "", "array")
{
	assert_eq(sizeof(array<int>), sizeof(int*)+sizeof(size_t));
	
	{
		array<int> x = { 1, 2, 3 };
		assert_eq(x[0], 1);
		assert_eq(x[1], 2);
		assert_eq(x[2], 3);
	}
	
	{
		array<int> x = { 1, 2, 3 };
		assert_eq(x.pop(1), 2);
		assert_eq(x[0], 1);
		assert_eq(x[1], 3);
	}
	
	{
		//passes if it does not leak memory
		class glutton {
			array<byte> food;
		public:
			glutton() { food.resize(1000); }
		};
		array<glutton> x;
		for (int i=0;i<1000;i++)
		{
			x.append();
		}
	}
}


static string tostring(array<bool> b)
{
	string ret;
	for (size_t i=0;i<b.size();i++) ret += b[i]?"1":"0";
	return ret;
}
static string ones_zeroes(int ones, int zeroes)
{
	string ret;
	for (int i=0;i<ones;i++) ret+="1";
	for (int i=0;i<zeroes;i++) ret+="0";
	return ret;
}
test("array<bool>", "", "array")
{
	for (size_t up=0;up<128;up+=13)
	for (size_t down=0;down<=up;down+=13)
	{
		array<bool> b;
		for (size_t i=0;i<up;i++)
		{
			assert_eq(b.size(), i);
			b.append(true);
		}
		assert_eq(b.size(), up);
		
		b.resize(down);
		assert_eq(tostring(b), ones_zeroes(down, 0));
		assert_eq(b.size(), down);
		for (size_t i=0;i<b.size();i++)
		{
			if (i<down) assert(b[i]);
			else assert(!b[i]);
		}
		assert_eq(b.size(), down);
		
		b.resize(up);
		assert_eq(tostring(b), ones_zeroes(down, up-down));
		assert_eq(b.size(), up);
		for (size_t i=0;i<b.size();i++)
		{
			if (i<down) assert(b[i]);
			else assert(!b[i]);
		}
	}
	
	{
		array<bool> b;
		b.resize(8);
		b[0] = true;
		b[1] = true;
		b[2] = true;
		b[3] = true;
		b[4] = true;
		b[5] = true;
		b[6] = true;
		b[7] = true;
		
		b.reset();
		b.resize(16);
	}
	
	{
		array<bool> b;
		b.resize(65);
		b.resize(64);
		assert_eq(b.size(), 64);
	}
	
	{
		array<int> n;
		n.resize(32);
		n = n.skip(32);
		assert_eq(n.size(), 0);
	}
}
#endif
