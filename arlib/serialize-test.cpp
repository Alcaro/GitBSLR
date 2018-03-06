#include "serialize.h"
#include "set.h"
#include "test.h"

#ifdef ARLIB_TEST
struct ser1 {
	int a;
	int b;
	
	SERIALIZE(a, b);
};

struct ser2 {
	ser1 c;
	ser1 d;
	
	SERIALIZE(c, d);
};

struct ser3 {
	int a;
	int b;
	int c;
	int d;
	int e;
	int f;
	int g;
	int h;
	
	SERIALIZE(a, b, c, d, e, f, g, h);
};

struct ser4 {
	ser3 mem;
	int count = 0;
	template<typename T> void serialize(T& s) { mem.serialize(s); count++; }
};

struct ser5 {
	array<int> data;
	SERIALIZE(data);
};

struct ser6 {
	array<ser1> data;
	SERIALIZE(data);
};

struct ser7 {
	ser5 par;
	SERIALIZE(par);
};

struct ser8 {
	//signed char a;
	//signed short b;
	//signed int c;
	//signed long d;
	//signed long long e;
	unsigned char f;
	unsigned short g;
	unsigned int h;
	unsigned long i;
	unsigned long long j;
	
	template<typename T>
	void serialize(T& s)
	{
		//s.hex("a", a);
		//s.hex("b", b);
		//s.hex("c", c);
		//s.hex("d", d);
		//s.hex("e", e);
		s.hex("f", f);
		s.hex("g", g);
		s.hex("h", h);
		s.hex("i", i);
		s.hex("j", j);
	}
};

struct ser9 {
	byte foo[4];
	array<byte> bar;
	
	template<typename T>
	void serialize(T& s)
	{
		s.hex("foo", arrayvieww<byte>(foo));
		s.hex("bar", bar);
	}
};

struct ser10 {
	string data;
	SERIALIZE(data);
};

struct ser11 {
	set<string> data;
	SERIALIZE(data);
};

struct ser12 {
	map<string,string> data;
	SERIALIZE(data);
};

test("BML serialization", "bml", "serialize")
{
	{
		ser1 item;
		item.a = 1;
		item.b = 2;
		
		assert_eq(bmlserialize(item), "a=1\nb=2");
	}
	
	{
		ser2 item;
		item.c.a = 1;
		item.c.b = 2;
		item.d.a = 3;
		item.d.b = 4;
		assert_eq(bmlserialize(item), "c a=1 b=2\nd a=3 b=4");
	}
	
	{
		ser5 item;
		item.data.append(1);
		item.data.append(2);
		item.data.append(3);
		assert_eq(bmlserialize(item), "data=1\ndata=2\ndata=3");
	}
	
	{
		ser6 item;
		item.data.append();
		item.data.append();
		item.data[0].a=1;
		item.data[0].b=2;
		item.data[1].a=3;
		item.data[1].b=4;
		assert_eq(bmlserialize(item), "data a=1 b=2\ndata a=3 b=4");
	}
	
	{
		ser7 item;
		item.par.data.append(1);
		item.par.data.append(2);
		item.par.data.append(3);
		assert_eq(bmlserialize(item), "par data=1 data=2 data=3");
	}
	
	{
		ser8 item;
		item.f = 0xAA;
		item.g = 0xAAAA;
		item.h = 0xAAAAAAAA;
		item.i = 0xAAAAAAAA; // this could have another eight As on linux, but why would I even do that
		item.j = 0xAAAAAAAAAAAAAAAA;
		assert_eq(bmlserialize(item), "f=AA\ng=AAAA\nh=AAAAAAAA\ni=AAAAAAAA\nj=AAAAAAAAAAAAAAAA");
	}
	
	{
		ser9 item;
		item.foo[0] = 0x12;
		item.foo[1] = 0x34;
		item.foo[2] = 0x56;
		item.foo[3] = 0x78;
		item.bar.append(0x12);
		item.bar.append(0x34);
		item.bar.append(0x56);
		item.bar.append(0x78);
		assert_eq(bmlserialize(item), "foo=12345678\nbar=12345678");
	}
	
	{
		ser10 item;
		item.data = "test";
		assert_eq(bmlserialize(item), "data=test");
	}
	
	{
		ser11 item;
		item.data.add("foo");
		item.data.add("test");
		item.data.add("C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		//the set is unordered, this can give spurious failures
		assert_eq(bmlserialize(item), "data=test\ndata=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"\ndata=foo");
	}
	
	{
		ser12 item;
		item.data.insert("foo", "bar");
		item.data.insert("test", "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		item.data.insert("C:/Users/Administrator/My Documents/!TOP SECRET!.docx", "test");
		assert_eq(bmlserialize(item), "data"
		                              " test=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\""
		                              " -C-3A-2FUsers-2FAdministrator-2FMy-20Documents-2F-21TOP-20SECRET-21.docx=test"
		                              " foo=bar");
	}
}

test("BML deserialization", "bml", "serialize")
{
	{
		ser1 item = bmlunserialize<ser1>("a=1\nb=2");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	
	{
		ser2 item = bmlunserialize<ser2>("c a=1 b=2\nd a=3 b=4");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//the system should not be order-sensitive
	//in case of dupes, last one should win; extraneous nodes should be cleanly ignored
	{
		ser2 item = bmlunserialize<ser2>("d b=4 a=3 q\nc a=42 b=2 a=1\nq");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//the system is allowed to loop, but only if there's bogus or extraneous nodes
	//we want O(n) runtime for a clean document, so ensure no looping
	//this includes missing and duplicate elements, both of which are possible for serialized arrays
	{
		ser4 item = bmlunserialize<ser4>("a=1\nb=2\nd=4\ne=5\ne=5\nf=6");
		assert_eq(item.count, 1);
	}
	
	{
		ser5 item = bmlunserialize<ser5>("data=1\ndata=2\ndata=3");
		assert_eq(item.data.size(), 3);
		assert_eq(item.data[0], 1);
		assert_eq(item.data[1], 2);
		assert_eq(item.data[2], 3);
	}
	
	{
		ser6 item = bmlunserialize<ser6>("data a=1 b=2\ndata a=3 b=4");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		ser7 item = bmlunserialize<ser7>("par data=1 data=2 data=3");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser8 item = bmlunserialize<ser8>("f=AA\ng=AAAA\nh=AAAAAAAA\ni=AAAAAAAA\nj=AAAAAAAAAAAAAAAA");
		assert_eq(item.f, 0xAA);
		assert_eq(item.g, 0xAAAA);
		assert_eq(item.h, 0xAAAAAAAA);
		assert_eq(item.i, 0xAAAAAAAA);
		assert_eq(item.j, 0xAAAAAAAAAAAAAAAA);
	}
	
	{
		ser9 item = bmlunserialize<ser9>("foo=12345678\nbar=12345678");
		assert_eq(item.foo[0], 0x12);
		assert_eq(item.foo[1], 0x34);
		assert_eq(item.foo[2], 0x56);
		assert_eq(item.foo[3], 0x78);
		assert_eq(item.bar.size(), 4);
		assert_eq(item.bar[0], 0x12);
		assert_eq(item.bar[1], 0x34);
		assert_eq(item.bar[2], 0x56);
		assert_eq(item.bar[3], 0x78);
	}
	
	{
		ser10 item = bmlunserialize<ser10>("data=test");
		assert_eq(item.data, "test");
	}
	
	{
		ser11 item = bmlunserialize<ser11>("data=foo\ndata=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"\ndata=test");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"));
	}
	
	{
		ser12 item = bmlunserialize<ser12>("data"
		                                   " foo=bar"
		                                   " -C-3A-2FUsers-2FAdministrator-2FMy-20Documents-2F-21TOP-20SECRET-21.docx=test"
		                                   " test=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"");
		assert_eq(item.data.get("foo"), "bar");
		assert_eq(item.data.get("test"), "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		assert_eq(item.data.get("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"), "test");
	}
	
	{
		cstring bml = R"(
a
 c
  e=0
  f=0
 d
  e=3
  f=4
a
 c
  e=1
 c
  f=2
 q
b
 d
  e=0
  f=0
  q=0
  f=8
  q f=0
  e=7
 c
 c
 c
  e=5
  f=6
q
a
b
 c
 d
c e=0
q
)";
		int ace = 9;
		int acf = 9;
		int ade = 9;
		int adf = 9;
		int bce = 9;
		int bcf = 9;
		int bde = 9;
		int bdf = 9;
		
		//this is some pretty ugly code, it's not meant to be used like this. real uses look better
		bmlunserialize_impl s(bml);
		
		ser_enter(s)
		{
			//with properly written serializers, while/if makes no difference
			//TODO: find a way to represent these operations that serializer can use too
			while (s.next() == "a") ser_enter(s)
			{
				while (s.next() == "c") ser_enter(s)
				{
					s.item("e", ace);
					s.item("f", acf);
				}
				while (s.next() == "d") ser_enter(s)
				{
					s.item("e", ade);
					s.item("f", adf);
				}
			}
			if (s.next() == "b") ser_enter(s)
			{
				if (s.next() == "c") ser_enter(s)
				{
					s.item("e", bce);
					s.item("f", bcf);
				}
				if (s.next() == "d") ser_enter(s)
				{
					s.item("e", bde);
					s.item("f", bdf);
				}
			}
		}
		
		string values = tostring(ace)+tostring(acf)+tostring(ade)+tostring(adf)+
		                tostring(bce)+tostring(bcf)+tostring(bde)+tostring(bdf);
		assert_eq(values, "12345678");
		assert_eq(ace, 1);
		assert_eq(acf, 2);
		assert_eq(ade, 3);
		assert_eq(adf, 4);
		assert_eq(bce, 5);
		assert_eq(bcf, 6);
		assert_eq(bde, 7);
		assert_eq(bdf, 8);
	}
}

test("JSON deserialization", "json", "serialize")
{
	{
		int x = jsonunserialize<int>("42");
		assert_eq(x, 42);
	}
	
	{
		ser1 item = jsonunserialize<ser1>("{\"a\":1,\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	
	{
		ser2 item = jsonunserialize<ser2>("{ \"c\": {\"a\":1,\"b\":2}, \"d\": {\"a\":3,\"b\":4} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//the system should not be order-sensitive
	{
		ser2 item = jsonunserialize<ser2>("{ \"d\": {\"b\":4,\"a\":3}, \"c\": {\"a\":1,\"b\":2} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//in case of dupes, last one should win; extraneous nodes should be cleanly ignored
	{
		ser1 item = jsonunserialize<ser1>("{ \"a\":1, \"b\":2, \"q\":0, \"a\":3, \"a\":4 }");
		assert_eq(item.a, 4);
		assert_eq(item.b, 2);
	}
	
	//make sure this one does not give an infinite loop
	{
		jsonunserialize<ser1>("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }");
	}
	
	//the system is allowed to loop, but only if there's bogus or extraneous nodes
	//we want O(n) runtime for a clean document, so ensure no looping
	//this includes missing and duplicate elements, both of which are possible for serialized arrays
	{
		ser4 item = jsonunserialize<ser4>("{ \"a\":1, \"b\":2, \"c\":3, \"d\":4, \"e\":5, \"f\":6 }");
		assert_eq(item.count, 1);
	}
	
	{
		ser5 item = jsonunserialize<ser5>("{ \"data\": [ 1, 2, 3 ] }");
		assert_eq(item.data.size(), 3);
		assert_eq(item.data[0], 1);
		assert_eq(item.data[1], 2);
		assert_eq(item.data[2], 3);
	}
	
	{
		ser6 item = jsonunserialize<ser6>("{ \"data\": [ { \"a\":1,\"b\":2 }, { \"a\":3,\"b\":4 } ] }");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		//some of these tests make no sense for json...
		ser7 item = jsonunserialize<ser7>("{ \"par\": { \"data\": [ 1, 2, 3 ] } }");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser8 item = jsonunserialize<ser8>("{ \"f\": 100, \"g\": 10000, \"h\": 1000000000, \"i\": 1000000000, \"j\": 1000000000000000000 } ");
		assert_eq(item.f, 100);
		assert_eq(item.g, 10000);
		assert_eq(item.h, 1000000000);
		assert_eq(item.i, 1000000000);
		assert_eq(item.j, 1000000000000000000ull); // this is the hex test object, json doesn't support hex
	}
	
	{
		ser9 item = jsonunserialize<ser9>("{ \"foo\": \"12345678\", \"bar\": \"12345678\" }");
		assert_eq(item.foo[0], 0x12);
		assert_eq(item.foo[1], 0x34);
		assert_eq(item.foo[2], 0x56);
		assert_eq(item.foo[3], 0x78);
		assert_eq(item.bar.size(), 4);
		assert_eq(item.bar[0], 0x12);
		assert_eq(item.bar[1], 0x34);
		assert_eq(item.bar[2], 0x56);
		assert_eq(item.bar[3], 0x78);
	}
	
	{
		ser10 item = jsonunserialize<ser10>("{ \"data\": \"test\" }");
		assert_eq(item.data, "test");
	}
	
	{
		ser11 item = jsonunserialize<ser11>("{ \"data\": [ \"foo\", \"test\", \"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\" ] }");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"));
	}
	
	{
		ser12 item = jsonunserialize<ser12>(
		    "{ \"data\": { \"foo\": \"bar\", "
		                  "\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\": \"test\", "
		                  "\"test\": \"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\" } }");
		assert_eq(item.data.get("foo"), "bar");
		assert_eq(item.data.get("test"), "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		assert_eq(item.data.get("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"), "test");
	}
}
#endif
