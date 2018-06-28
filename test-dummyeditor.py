#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

#this thing's sole purpose is to fail if GitBSLR is loaded into the process

import os
import sys
os.chdir(".")
os.readlink("/proc/self/exe")

open(sys.argv[1],"wt").write("GitBSLR test")
