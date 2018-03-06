#include "function.h"
#include "test.h"

namespace {
static int r42() { return 42; }
static int add42(int x) { return x+42; }
class adder {
	int x;
public:
	adder(int x) : x(x)
	{
		n_adders++;
	}
	~adder() { n_adders--; }
	int add(int y) { return x+y; }
	function<int(int)> wrap() { return bind_this(&adder::add); }
	
	static int n_adders;
};
int adder::n_adders = 0;
}

test("function", "", "function")
{
	{
		function<int()> r42w = r42;
		assert_eq(r42w(), 42);
	}
	
	{
		function<int(int)> a42w = add42;
		assert_eq(a42w(10), 52);
	}
	
	{
		adder a42(42);
		function<int(int)> a42w = a42.wrap();
		assert_eq(a42w(10), 52);
		
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	{
		function<int(int)> a42w = bind_ptr_del(&adder::add, new adder(42));
		assert_eq(a42w(10), 52);
		
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	{
		function<int(int)> a42w = bind_lambda([](int x)->int { return x+42; });
		assert_eq(a42w(10), 52);
	}
	
	{
		int n = 42;
		function<int(int)> a42w = bind_lambda([=](int x)->int { return x+n; });
		n = -42;
		assert_eq(a42w(10), 52);
	}
	
	{
		int n = -42;
		function<int(int)> a42w = bind_lambda([&](int x)->int { return x+n; });
		n = 42;
		assert_eq(a42w(10), 52);
	}
	
	{
		int n = 42;
		function<int(int)> a42w = bind_lambda([](int* xp, int x)->int { return x+*xp; }, &n);
		assert_eq(a42w(10), 52);
	}
}
