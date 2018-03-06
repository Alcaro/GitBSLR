//TODO:
//- window.h: remove pointers
//- window.h: remove varargs
//- msvc compat: add some define that, if absent, enables every feature

//WARNING: Arlib comes with zero stability guarantees. It can and will change in arbitrary ways, for any reason and at any time.
//if anyone whines about antivirus, https://arstechnica.com/information-technology/2017/01/antivirus-is-bad/

#pragma once
#include "global.h"

#include "bml.h"
#include "bytepipe.h"
#include "bytestream.h"
#include "containers.h"
#include "crc32.h"
#include "endian.h"
#include "file.h"
#include "function.h"
#include "html.h"
#include "init.h"
#include "intwrap.h"
#include "json.h"
#include "linq.h"
#include "os.h"
#include "process.h"
#include "runloop.h"
#include "safeint.h"
#include "serialize.h"
#include "stringconv.h"
#include "string.h"
#include "test.h"
#include "zip.h"

//no ifdef on this one, it contains some dummy implementations if threads are disabled
#include "thread/thread.h"

#if !defined(ARGUI_NONE) && !defined(ARGUI_WINDOWS) && !defined(ARGUI_GTK3)
#define ARGUI_NONE
#endif
#ifndef ARGUI_NONE
#include "gui/window.h"
#endif

#ifdef ARLIB_OPENGL
#include "opengl/aropengl.h"
#endif

#ifdef ARLIB_WUTF
#include "wutf/wutf.h"
#endif

#ifdef ARLIB_SANDBOX
#include "sandbox/sandbox.h"
#endif

#ifdef ARLIB_SOCKET
#include "socket/socket.h"
#include "dns.h"
#include "http.h"
#include "websocket.h"
#endif
