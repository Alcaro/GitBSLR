#object filename structure:
#obj/RULE___OBJNAME___PATH.o
#RULE is the set of build flags for this file, nearly always DEFAULT
#OBJNAME is the OBJNAME= make parameter, or a default value
#PATH is the source filename, including extension, relative to project root, with slashes replaced with double underscore
#example: obj/DEFAULT___linux___arlib__file-posix.cpp.o

arlib-default: arlib-default2

ifeq (,$(wildcard arlib/string.cpp))
  $(error wrong build directory, go up one level)
endif

SPACE :=
SPACE +=

define LF


endef

ifeq ($(OS),Windows_NT)
  OS = windows
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    OS = linux
  else ifeq ($(UNAME_S),Darwin)
    OS = osx
  else ifeq ($(UNAME_S),)
    $(error couldn't execute uname -s)
  else
    $(error what weird OS is $(UNAME_S)?)
  endif
endif

ifeq ($(V),)
Q = @
ECHOQ = @echo
else
Q =
#TODO: windows version
ECHOQ = @true
endif

ifneq (,$(findstring test,$(MAKECMDGOALS)))
  SELFTEST = 1
endif
ifneq (,$(findstring check,$(MAKECMDGOALS)))
  SELFTEST = 1
endif

ARTYPE ?= exe
ARGUI ?= 0
AROPENGL ?= 0
AROPENGL_D3DSYNC ?= 1
ARIMAGE ?= 0
ARTHREAD ?= 0
ARSANDBOX ?= 0
ARWUTF ?= 0
ARSOCKET ?= 0
ARSOCKET_SSL ?= openssl

OPT ?= 0
ifeq ($(OPT),0)
  DEBUG ?= 1
else
  DEBUG ?= 0
endif

CC = gcc
CFLAGS =
CXX = g++
CXXFLAGS = $(CFLAGS)
LD = g++
LFLAGS =
OBJNAME =
CCXXFLAGS = -fvisibility=hidden -Wall -Wmissing-declarations
#doesn't seem to do anything, and CFLAGS is used for c++ too which throws warnings all over
#CFLAGS += -Wmissing-prototypes
ifneq ($(EXCEPTIONS),1)
  ifneq ($(SELFTEST),1)
    CCXXFLAGS += -fno-exceptions
  endif
endif

#double gcc bug combo:
#(1) GCC hates this pattern:
#//#define foo(a,b,c) \
#//  bar(a) \
#//  bar(b) \
#//  bar(c)
# to my knowledge unreported
#(2) '#pragma GCC diagnostic ignored "-Wcomment"' does nothing
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431
CCXXFLAGS += -Wno-comment

EXESUFFIX =
CONF_CXXFLAGS += $(CONF_CFLAGS)

ifeq ($(OS),linux)
  CONF_LFLAGS += -ldl -Wl,-z,relro,-z,now,--no-undefined
  ifeq ($(ARTYPE),dll)
    CONF_CFLAGS += -fPIC
    CONF_LFLAGS += -shared
    EXESUFFIX = .so
  endif
  ifeq ($(ARTHREADS),1)
    CONF_CFLAGS += -pthread
    CONF_LFLAGS += -pthread
  endif
  OBJNAME = linux
  TESTRUNNER = valgrind --num-callers=100
endif
ifeq ($(TESTRUNNER),gdb)
  override TESTRUNNER = gdb --args
endif

ifeq ($(OS),windows)
  EXESUFFIX = .exe
#  OBJ += obj/resource$(OBJNAME).o
#  RC = windres
#  RCFLAGS =
#obj/resource$(OBJNAME).o: ico/*
#	$(RC) $(RCFLAGS) ico/minir.rc obj/resource$(OBJNAME).o
  OBJNAME = windows
  $(error check if -Wl,nxcompat does anything or if it defaults to true)
endif

OPTFLAGS := -Os -fomit-frame-pointer -fmerge-all-constants -fvisibility=hidden
OPTFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
OPTFLAGS += -ffunction-sections -fdata-sections
OPTFLAGS += -fno-ident
OPTFLAGS += -Werror -DNDEBUG
OPTLFLAGS := -Wl,--gc-sections -Wl,--build-id=none
#OPTLFLAGS += -Wl,--print-map
ifneq ($(DEBUG),1)
  ifneq ($(STRIP),0)
    OPTLFLAGS += -s
  endif
endif

ifeq ($(DEBUG),1)
  CFLAGS += -g -DDEBUG
endif
ifneq ($(OPT),0)
  #OPT=1 DEBUG=1 should prioritize optimization over debugging; OPT=1 DEBUG=1 then 'strip' should be identical to OPT=1 DEBUG=0
  CONF_CFLAGS += $(OPTFLAGS) -UDEBUG
  CONF_LFLAGS += $(OPTLFLAGS)
  ifeq ($(OPT),spd)
    CONF_CFLAGS += -O3
  endif
  OBJNAME := $(OBJNAME)-opt
endif

ifeq ($(PROFILE),gen)
  CONF_CFLAGS += -fprofile-generate
  CONF_LFLAGS += -lgcov
endif
ifeq ($(PROFILE),use)
  CONF_CFLAGS += -fprofile-use -fprofile-correction
endif

CONF_CFLAGS += -DARLIB_OBJNAME=$(OBJNAME)

OUTNAME = $(PROGRAM)$(EXESUFFIX)

#OBJMANGLE(rule,sources) - takes C/C++ source files and returns the mangled name under the specified rule
OBJMANGLE = $(patsubst %,obj/$1___$(OBJNAME)___%.o,$(subst /,__,$2))
#SOURCENAME(obj) - takes a .o file, returned from OBJMANGLE, and returns the corresponding source file
#does not handle multi-file inputs, use $(foreach)
SOURCENAME = $(strip $(subst __,/,$(lastword $(subst ___,$(SPACE),$(patsubst obj/%.o,%,$1)))))
#DOMAINNAME(obj) - takes a .o file, returned from OBJMANGLE, and returns the corresponding domain
DOMAINNAME = $(firstword $(subst ___,$(SPACE),$(patsubst obj/%.o,%,$1)))

CFLAGS_DEFAULT =
CFLAGS_NOWARN = -w

.SECONDEXPANSION:
.DELETE_ON_ERROR:

obj/%.c.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) CC $<
	$(Q)$(CC) $(TRUE_CFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.cpp.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) $(TRUE_CXXFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@

SOURCES += *.cpp
SOURCES_ARLIB := arlib/*.cpp

ifneq ($(SELFTEST),)
  ifeq ($(ARLIB_MAIN),)
    CONF_CFLAGS += -DARLIB_TEST
    CFLAGS_ARLIB += -UARLIB_TEST -DARLIB_TESTRUNNER
  else
    # with SOURCES blanked, the real main isn't compiled
    SOURCES :=
    CFLAGS_ARLIB += -DARLIB_TEST -DARLIB_TESTRUNNER -DARLIB_TEST_ARLIB
  endif
  OBJNAME := $(OBJNAME)-test
endif

ifeq ($(ARGUI),1)
  SOURCES_ARLIB += arlib/gui/*.cpp
  ifeq ($(OS),windows)
    DEFINES += ARGUI_WINDOWS
    CONF_LFLAGS += -lgdi32 -lcomctl32 -lcomdlg32
  endif
  ifeq ($(OS),linux)
    DEFINES += ARGUI_GTK3 ARGUIPROT_X11
    CONF_CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
    CONF_LFLAGS += -ldl -lX11 $(shell pkg-config --libs gtk+-3.0)
    ifneq ($(OPT),0)
      # don't know if this has ever caught anything except NULLs, but they're harmless on debug
      # but they do waste space on release builds.
      CONF_CFLAGS += -DG_DISABLE_CAST_CHECKS
    endif
  endif
else
  DEFINES += ARGUI_NONE
endif

ifeq ($(AROPENGL),1)
  ifeq ($(ARGUI),0)
    $(error can't use OpenGL without the GUI)
  endif
  SOURCES_ARLIB += arlib/opengl/*.cpp
  DEFINES += ARLIB_OPENGL
  ifeq ($(OS),linux)
    CONF_LFLAGS += -ldl
  endif
  ifeq ($(AROPENGL_D3DSYNC),1)
    DEFINES += AROPENGL_D3DSYNC
  endif
endif

ifeq ($(ARTHREAD),1)
  SOURCES_ARLIB += arlib/thread/*.cpp
  DEFINES += ARLIB_THREAD
  ifeq ($(OS),linux)
    CONF_CFLAGS += -pthread
    CONF_LFLAGS += -pthread
  endif
endif

ifeq ($(ARSANDBOX),1)
  include arlib/sandbox/Makefile
endif

ifeq ($(ARWUTF),1)
  SOURCES_ARLIB += arlib/wutf/*.cpp
  DEFINES += ARLIB_WUTF
endif

ifeq ($(ARSOCKET),1)
  SOURCES_ARLIB += arlib/socket/*.cpp
  DEFINES += ARLIB_SOCKET
  
  ifeq ($(OS),windows)
    CONF_LFLAGS += -lws2_32
    ifdef ARSOCKET_SSL_WINDOWS
      ARSOCKET_SSL := $(ARSOCKET_SSL_WINDOWS)
    else
      ARSOCKET_SSL := schannel
    endif
  endif
  
  #TODO: allow using multiple at once, the tests could use that
  ifeq ($(ARSOCKET_SSL),no)
    #no SSL
    #socketssl will still be available at compile time, but fails linking
  else ifeq ($(ARSOCKET_SSL),schannel)
    CFLAGS_ARLIB += -DARLIB_SSL_SCHANNEL
    #TODO: check if this is still needed
    #CONF_LFLAGS += -lcrypt32 -lsecur32
  else ifeq ($(ARSOCKET_SSL),openssl)
    CFLAGS_ARLIB += -DARLIB_SSL_OPENSSL
    CONF_LFLAGS += -lssl -lcrypto
  else ifeq ($(ARSOCKET_SSL),gnutls)
    CFLAGS_ARLIB += -DARLIB_SSL_GNUTLS
    CONF_LFLAGS += -lgnutls
  else ifeq ($(ARSOCKET_SSL),bearssl)
    CFLAGS_ARLIB += -DARLIB_SSL_BEARSSL
    SOURCES_ARLIB += arlib/socket/bear-ser.c
    OBJS_LATE += arlib/deps/bearssl-0.5/build/libbearssl.a
arlib/deps/bearssl-0.5/build/libbearssl.a:
	$(MAKE) -C arlib/deps/bearssl-0.5/ CFLAGS='$(OPTFLAGS)' lib
  else ifeq ($(ARSOCKET_SSL),tlse)
    CFLAGS_ARLIB += -DARLIB_SSL_TLSE
    SOURCES_NOWARN += arlib/deps/tlse.c
  else
    $(error unknown SSL library)
  endif
endif

SOURCES_NOWARN += arlib/deps/miniz.c

#CFLAGS_ARLIB += $(patsubst %,-D%,$(DEFINES))
CONF_CFLAGS += $(patsubst %,-D%,$(DEFINES))

TRUE_CFLAGS := -std=c99 $(CCXXFLAGS) $(CFLAGS) $(CONF_CFLAGS)
TRUE_CXXFLAGS := -std=c++11 -fno-rtti $(CCXXFLAGS) $(CXXFLAGS) $(CONF_CXXFLAGS)
TRUE_LFLAGS := $(LFLAGS) -fvisibility=hidden $(CONF_LFLAGS)

ifeq ($(OS),windows)
  TRUE_LFLAGS += -static-libgcc 
endif

.force:

#On Windows, cleaning up the object directory is expected to be done with 'del /q obj\*' in a batch script.
clean:
	rm -f obj/*

clean-prof:
	rm -f obj/*.o

obj:
	mkdir obj

#ARLIB_H := $(patsubst %,\#define %$(LF),$(DEFINES))
#arlib.h: Makefile
#	$(eval export ARLIB_H)
#	echo "$$ARLIB_H" > arlib.h
#	echo '#include "arlib/arlib.h"' >> arlib.h


DOMAINS += DEFAULT ARLIB NOWARN

SOURCES_DEFAULT := $(SOURCES)
OBJS += $(foreach domain,$(sort $(DOMAINS)),$(call OBJMANGLE,$(domain),$(wildcard $(SOURCES_$(domain)))))
OBJS += $(OBJS_LATE)

all: $(OUTNAME)
arlib-default2: $(OUTNAME)
$(OUTNAME): $(OBJS)
	$(ECHOQ) LD $@
	$(Q)$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm



ifneq ($(SELFTEST),)
obj/arlibtest$(EXESUFFIX): $(OBJS)
	$(ECHOQ) LD $@
	$(Q)$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm

test: obj/arlibtest$(EXESUFFIX)
	$(TESTRUNNER) obj/arlibtest$(EXESUFFIX)
check: test
test-all: obj/arlibtest$(EXESUFFIX)
	$(TESTRUNNER) obj/arlibtest$(EXESUFFIX) --all
test-all-twice: obj/arlibtest$(EXESUFFIX)
	$(TESTRUNNER) obj/arlibtest$(EXESUFFIX) --all --twice
endif



ifeq ($(MAKECMDGOALS),me a sandwich)
.PHONY: me a sandwich
me a:
	@:

sandwich:
	@[ `id -u` -ne 0 ] && echo "What? Make it yourself." || echo Okay.
endif
