# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

ifeq ($(shell uname -s),Darwin)
  $(error This program does not function on macOS. I couldn't find a functional LD_PRELOAD equivalent.)
endif

OPT ?= 0
ifeq ($(OPT),0)
  DEBUG ?= 1
else
  DEBUG ?= 0
endif

CFLAGS = -g
CXXFLAGS = $(CFLAGS)

TRUE_FLAGS := -std=c++98 -fno-rtti -fvisibility=hidden
TRUE_FLAGS += -fvisibility=hidden -Wall -Wmissing-declarations -pipe -fno-exceptions
TRUE_FLAGS += -fPIC -ldl -Wl,-z,relro,-z,now,--no-undefined -shared

ifneq ($(OPT),0)
  TRUE_FLAGS += -Os -fomit-frame-pointer -fmerge-all-constants -fvisibility=hidden
  TRUE_FLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
  TRUE_FLAGS += -ffunction-sections -fdata-sections
  TRUE_FLAGS += -fno-ident -DNDEBUG
  TRUE_FLAGS += -Wl,--gc-sections,--build-id=none,--hash-style=gnu,--relax
  ifneq ($(DEBUG),1)
    TRUE_FLAGS += -s
    CFLAGS =
  endif
endif

TRUE_FLAGS += $(CXXFLAGS) $(LFLAGS)

gitbslr.so: main.cpp
	$(CXX) $+ $(TRUE_FLAGS) -o $@ -lm

clean:
	rm gitbslr.so

install:
	./install.sh
uninstall:
	./install.sh uninstall

test: gitbslr.so
	sh test1.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test2.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test3.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test4.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test5.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test6.sh | tee /dev/stderr | grep -q 'Test passed'
	rm -rf test/
	echo All tests passed
check: test

.PHONY: clean install uninstall test check
