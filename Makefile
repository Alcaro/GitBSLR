# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

OPT ?= 0
ifeq ($(OPT),0)
  DEBUG ?= 1
else
  DEBUG ?= 0
endif

CFLAGS = -g
CXXFLAGS = $(CFLAGS)

TRUE_FLAGS := -std=c++11 -fno-rtti -fvisibility=hidden
TRUE_FLAGS += -fvisibility=hidden -Wall -Wmissing-declarations -pipe -fno-exceptions
#TODO: remove this one
TRUE_FLAGS += -Wno-comment
TRUE_FLAGS += -fPIC -ldl -Wl,-z,relro,-z,now,--no-undefined -shared

ifneq ($(OPT),0)
  TRUE_FLAGS += -Os -fomit-frame-pointer -fmerge-all-constants -fvisibility=hidden
  TRUE_FLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
  TRUE_FLAGS += -ffunction-sections -fdata-sections
  TRUE_FLAGS += -fno-ident
  TRUE_FLAGS += -Werror -DNDEBUG
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
	rm -f gitbslr.so
