#include "bytepipe.h"
#include "test.h"

static void push(bytepipe& p, size_t& t_written, size_t request, size_t push)
{
	arrayvieww<byte> tmp = p.push_buf(request);
	assert(tmp.ptr() != NULL);
	assert(tmp.size() >= request);
	for (size_t i=0;i<push;i++)
	{
		tmp[i] = (++t_written) % 253;
	}
	p.push_done(push);
}

static void pull(bytepipe& p, size_t& t_read, size_t& t_written, bool all, size_t expect, size_t use)
{
	arrayview<byte> tmp = (all ? p.pull_buf_full() : p.pull_buf());
	assert_eq(p.remaining(), t_written-t_read);
	assert_eq(tmp.size(), expect);
	for (size_t i=0;i<use;i++)
	{
		assert_eq(tmp[i], (++t_read) % 253);
	}
	p.pull_done(use);
	assert_eq(p.remaining(), t_written-t_read);
}

test("bytepipe", "array", "bytepipe")
{
	size_t r = 0;
	size_t w = 0;
	
	{
		bytepipe p;
		testcall(push(p,w, 768, 768));
		testcall(push(p,w, 512, 512));
		testcall(pull(p,r,w, false, 768, 512));
		testcall(pull(p,r,w, false, 256, 256));
		testcall(pull(p,r,w, false, 512, 512));
		testcall(pull(p,r,w, false, 0, 0));
	}
	
	{
		bytepipe p;
		testcall(push(p,w, 2, 2));
		testcall(push(p,w, 1, 1));
		testcall(pull(p,r,w, false, 3, 3));
		testcall(push(p,w, 2, 2));
		testcall(push(p,w, 1023, 1023));
		testcall(pull(p,r,w, false, 2, 2));
		testcall(pull(p,r,w, false, 1023, 1023));
	}
	
	{
		bytepipe p;
		testcall(push(p,w, 1024, 1024));
		testcall(push(p,w, 1, 1));
		testcall(pull(p,r,w, false, 1024, 1023));
		testcall(pull(p,r,w, true, 2, 2));
		testcall(pull(p,r,w, true, 0, 0));
	}
	
	{
		bytepipe p;
		testcall(push(p,w, 768, 768));
		testcall(push(p,w, 512, 512));
		testcall(pull(p,r,w, true, 1280, 0));
		testcall(pull(p,r,w, false, 1280, 512));
		testcall(pull(p,r,w, false, 768, 768));
		testcall(pull(p,r,w, false, 0, 0));
		testcall(pull(p,r,w, true, 0, 0));
	}
	
	{
		bytepipe p;
		
		for (int pass_outer=0;pass_outer<3;pass_outer++)
		{
			const char * text = "test string\n";
			
			for (int pass_inner=0;pass_inner<3;pass_inner++)
			{
				for (int i=0;text[i];i++)
				{
					byte tmp[1] = { (byte)text[i] };
					p.push(arrayview<byte>(tmp));
					arrayview<byte> line = p.pull_line();
					if (text[i]!='\n') assert(!line);
					else
					{
						assert_eq(string(line), text);
						p.pull_done(line);
					}
				}
			}
			
			for (int pass_inner=0;pass_inner<3;pass_inner++)
			{
				p.push(arrayview<byte>((byte*)text, strlen(text)));
				arrayview<byte> line = p.pull_line();
				assert_eq(string(line), text);
				p.pull_done(line);
			}
			
			for (int pass_inner=0;pass_inner<3;pass_inner++)
			{
				p.push(arrayview<byte>((byte*)text, strlen(text)));
			}
			
			for (int pass_inner=0;pass_inner<3;pass_inner++)
			{
				arrayview<byte> line = p.pull_line();
				assert_eq(string(line), text);
				p.pull_done(line);
			}
			
			for (int pass_inner=0;pass_inner<3;pass_inner++)
			{
				arrayview<byte> line = p.pull_line();
				assert(!line);
			}
		}
	}
}
