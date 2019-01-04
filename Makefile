# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

EXT := .so
ifeq ($(shell uname -s),Darwin)
  EXT := .dylib
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
ifneq ($(shell uname -s),Darwin)
  TRUE_FLAGS += -fPIC -ldl -Wl,-z,relro,-z,now,--no-undefined -shared
else
  TRUE_FLAGS += -dynamiclib -undefined suppress
endif

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

gitbslr$(EXT): main.cpp
	$(CXX) $+ $(TRUE_FLAGS) -o $@ -lm

clean:
	rm gitbslr$(EXT)

install:
	./install.sh
uninstall:
	./install.sh uninstall

test: gitbslr.so
	sh test.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test2.sh | tee /dev/stderr | grep -q 'Test passed'
	sh test3.sh | tee /dev/stderr | grep -q 'Test passed'
	echo All tests passed
.PHONY: test
