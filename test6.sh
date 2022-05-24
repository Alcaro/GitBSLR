#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script tests what happens if cwd is a symlink to the repo.

mkdir                      test/repo
echo test >                test/repo/file
ln_sr test/repo            test/repolink

cd test/repolink
gitbslr init
gitbslr add file
gitbslr commit -m test
rm file
gitbslr checkout -f HEAD
rm file
gitbslr reset --hard
rm file

echo Test passed
