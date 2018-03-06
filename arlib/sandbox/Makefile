ifneq ($(OS),linux)
  $(error sandbox is only supported on Linux)
endif

SOURCES_ARLIB += arlib/sandbox/*.cpp
DEFINES += ARLIB_SANDBOX

#ensure we're usable as linker, with ASLR enabled
PRELOADER_CFLAGS := -fPIC
PRELOADER_LFLAGS := -static -shared
#runtime libraries aren't available, don't use them and don't use anything using them
PRELOADER_CFLAGS += -ffreestanding
PRELOADER_LFLAGS += -nostdlib -nostartfiles -nodefaultlibs
#gcc sure loves inserting calls to random stuff, force it not to
#I'd prefer if I could enable -fstack-protector, but the stack cookie is at %fs:40, which is a segfault
#I could set it up, but that'd require a bunch of assembly, leak memory, and confuse the real libc when it tries to set up %fs
#glibc seems built without stack protector, anyways, and the low amount of stack used by this thing is easy to audit by hand
PRELOADER_CFLAGS += -fno-stack-protector -fno-exceptions -fno-rtti -fno-jump-tables -fno-sync-libcalls
#nothing should nor can override our symbols, and allowing it causes relocation issues
PRELOADER_CFLAGS += -fvisibility=hidden
#AT&T assembly is ugly
#yes, there's only ~7 lines of asm in this thing, AT&T is still ugly
PRELOADER_CFLAGS += -masm=intel

#I could include TRUE_{C,L}FLAGS, but that includes silly stuff like -pthread and -lgtk-3, so just hardcode them
#CFLAGS/LFLAGS are set by command line only, empty by default, can safely be ignored
PRELOADER_CFLAGS += -DSANDBOX_INTERNAL -std=c++11 $(CFLAGS)
PRELOADER_CFLAGS += -Wall -Wmissing-declarations -Wno-comment
PRELOADER_LFLAGS += -Wl,-z,now $(LFLAGS)

ifneq ($(OPT),0)
  #not sure if gdb can find the debug flags of the sandbox child, or do anything useful with it at all
  #(strace works fine, but doesn't care about symbols)
  #but gdb can run the launcher without the sandbox, so keep debug by default anyways
  PRELOADER_CFLAGS += $(OPTFLAGS)
  PRELOADER_LFLAGS += $(OPTLFLAGS)
endif
ifeq ($(DEBUG),1)
  PRELOADER_CFLAGS += -g -DDEBUG
endif

#this could be in obj/, but if I put it there, Python is mandatory for building Arlib. I'd rather not.
arlib/sandbox/bpf.inc: arlib/sandbox/bpf.S arlib/sandbox/bpfasm.py
	python3 arlib/sandbox/bpfasm.py $< $@
$(call OBJMANGLE,ARLIB,arlib/sandbox/launch-linux-sand.cpp): arlib/sandbox/launch-linux-sand.cpp arlib/sandbox/bpf.inc | obj

PRELOADER_SOURCES := arlib/sandbox/preload-linux-sand.cpp arlib/sandbox/sysemu-linux-sand.cpp
PRELOADER_OBJS := $(call OBJMANGLE,arlibsand,$(wildcard $(PRELOADER_SOURCES)))
obj/arlibsand___%.cpp.o: $$(call SOURCENAME,$$@) arlib/sandbox/internal-linux-sand.h | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) $(PRELOADER_CFLAGS) -c $< -o $@

obj/sand-preload-$(OBJNAME).elf: $(PRELOADER_OBJS) | obj
	$(ECHOQ) LD $@
	$(Q)$(CXX) -D SANDBOX_INTERNAL $(PRELOADER_LFLAGS) $(PRELOADER_OBJS) -o $@
#ensure failure if this thing isn't 100% static
#requires .DELETE_ON_ERROR, which is set in Arlib's main makefile
	objdump -R $@ | grep -A500 -E 'DYNAMIC RELOCATION RECORDS$$' || true
	objdump -R $@ | grep -qF 'DYNAMIC RELOCATION RECORDS (none)'
$(call OBJMANGLE,ARLIB,arlib/sandbox/preload-linux-sand.cpp): arlib/sandbox/preload-linux-sand.cpp obj/sand-preload-$(OBJNAME).elf | obj
