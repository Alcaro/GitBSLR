#include "process.h"

#ifdef _WIN32
static string escapearg(cstring arg)
{
	//https://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
	//keeps getting bugged up, "foo" -> "\"foo\"" -> \foo\, and some commands (such as find) use different rules
	if (arg.contains(" ") && arg[0]!='"') return "\""+arg+"\"";
	return arg;
	//string ret = "";
	//bool needescape = false;
	//size_t numbackslash;
	//for (size_t j=0;j<arg.length();j++)
	//{
	//	char ch = arg[j];
	//	
	//	if (ch=='\\')
	//	{
	//		numbackslash++;
	//	}
	//	else if (ch=='"')
	//	{
	//		for (size_t i=0;i<numbackslash;i++) ret += "\\\\";
	//		ret += "\\\"";
	//		numbackslash = 0;
	//		needescape = true;
	//	}
	//	else
	//	{
	//		for (size_t i=0;i<numbackslash;i++) ret += "\\";
	//		ret += ch;
	//		numbackslash = 0;
	//		if (ch==' ') needescape = true;
	//	}
	//}
	//if (needescape) return "\""+ret+"\"";
	//else return arg;
}

bool process::launch(cstring prog, arrayview<string> args)
{
	string cmdline = escapearg(prog);
	for (cstring s : args)
	{
		cmdline += " "+escapearg(s);
	}
	
	STARTUPINFO sti;
	memset(&sti, 0, sizeof(sti));
	sti.cb = sizeof(sti);
	sti.dwFlags = STARTF_USESTDHANDLES;
	
	if (!CreatePipe(&sti.hStdInput, &this->stdin_h, NULL, 0)) return false;
	SetHandleInformation(sti.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	
	if (!CreatePipe(&this->stdout_h, &sti.hStdOutput, NULL, 0)) return false;
	SetHandleInformation(sti.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	
	if (this->stderr_split)
	{
		if (!CreatePipe(&this->stderr_h, &sti.hStdError, NULL, 0)) return false;
		SetHandleInformation(sti.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	}
	else
	{
		sti.hStdError = sti.hStdOutput;
		this->stderr_h = NULL;
	}
	
	PROCESS_INFORMATION pi;
	bool ok = (CreateProcess(NULL/*FIXME?*/, (char*)cmdline.bytes().ptr(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &sti, &pi));
	if (!ok) return false;
	
	CloseHandle(sti.hStdInput);
	CloseHandle(sti.hStdOutput);
	CloseHandle(sti.hStdError);
	
	this->proc = pi.hProcess;
	CloseHandle(pi.hThread);
	
	update();
	return true;
}

//returns whether it did anything
static bool update_piperead(HANDLE h, array<byte>& out, size_t limit)
{
	if (!h) return false;
	DWORD newbytes;
//printf("[");
	if (!PeekNamedPipe(h, NULL,0, NULL, &newbytes, NULL)) return false; // happens on process death
	if (out.size() + newbytes > limit) newbytes = limit-out.size();
//printf("]");
	if (newbytes==0) return false;
	size_t oldbytes = out.size();
	out.reserve_noinit(oldbytes+newbytes);
	DWORD actualnewbytes;
//printf("{");
	ReadFile(h, out.ptr()+oldbytes, newbytes, &actualnewbytes, NULL);
//printf("}");
	if (newbytes != actualnewbytes) out.resize(oldbytes+actualnewbytes);
	return true;
}

void process::update(bool sleep)
{
	if (this->proc)
	{
		if (WaitForSingleObject(this->proc, 0) == 0)
		{
			DWORD exitcode;
			GetExitCodeProcess(this->proc, &exitcode);
			this->exitcode = exitcode;
			CloseHandle(this->proc);
			this->proc = NULL;
		}
	}
	
	bool didsomething = false;
//static string last;
//string next = "IN="+tostring(stdin_buf.size())+" OUT="+tostring(stdout_buf.size());
//if(next!=last)puts(next);
//last=next;
	if (stdin_buf)
	{
		DWORD bytes = min(4096, stdin_buf.size());
//printf("(");
		WriteFile(stdin_h, stdin_buf.ptr(), bytes, &bytes, NULL);
//printf(")");
		if (bytes > 0) stdin_buf = stdin_buf.skip(bytes);
		didsomething = true;
	}
	else if (!this->stdin_open && this->stdin_h)
	{
//puts("DIE");
		CloseHandle(this->stdin_h);
		this->stdin_h = NULL;
	}
	
	didsomething |= update_piperead(stdout_h, stdout_buf, outmax-stderr_buf.size());
	didsomething |= update_piperead(stderr_h, stderr_buf, outmax-stdout_buf.size());
	
	if (stdout_buf.size()+stderr_buf.size() >= this->outmax)
	{
		if (stdout_h) CloseHandle(stdout_h);
		if (stderr_h) CloseHandle(stderr_h);
		stdout_h = NULL;
		stderr_h = NULL;
	}
	
	//horrible hack, but Windows can't sanely select() pipes
	//the closest match I can find is "asynchronous read of 1 byte; when it finishes, peek the pipe and read whatever else is there"
	//(or, for stdin, juggle two buffers, swapping them when the write finishes)
	//and that's too much effort.
	if (sleep && !didsomething) Sleep(1);
}

bool process::running(int* exitcode)
{
	update();
	if (exitcode && this->proc==NULL) *exitcode = this->exitcode;
	return (this->proc != NULL);
}

void process::wait(int* exitcode)
{
	this->stdin_open = false;
	while (this->proc) update(true);
	if (exitcode) *exitcode = this->exitcode;
}

void process::terminate()
{
	if (this->proc)
	{
		TerminateProcess(this->proc, 0);
		CloseHandle(this->proc);
		this->proc = NULL;
	}
}

process::~process()
{
	terminate();
	if (stdin_h) CloseHandle(stdin_h);
	if (stdout_h) CloseHandle(stdout_h);
	if (stderr_h) CloseHandle(stderr_h);
}
#endif
