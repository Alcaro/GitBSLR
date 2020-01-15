#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script tests symlinks to not-yet-existing targets.

mkdir                      test/repo/
mkdir                      test/repo/b/
mkdir                      test/repo/c/
mkdir                      test/repo/d/
echo test >                test/inaccessible
ln_sr test/repo/c/b        test/repo/a
ln_sr test/repo/c/b        test/repo/b/a
ln_sr test/repo/c/b        test/repo/b/b
ln_sr test/repo/c/b        test/repo/b/c
ln_sr test/repo/c/b        test/repo/c/a
echo test >                test/repo/c/b
ln_sr test/repo/c/b        test/repo/c/c
ln_sr test/repo/c/b        test/repo/d/a
ln_sr test/repo/c/b        test/repo/d/b
ln_sr test/repo/c/b        test/repo/d/c
ln_sr test/repo/c/b        test/repo/e
ln -s ../output/c/b        test/repo/fFAIL

ln_sr test/inaccessible    test/repo/gFAIL
ln -s ../output/c          test/repo/hFAIL
ln -s ../c/../c/b          test/repo/c/dFAIL

cd test/repo/
# gitbslr would rewrite the symlinks above, so use original git
git init
git add .
git commit -m test
cd ../../

mkdir test/output/
mv test/repo/.git/ test/output/.git/
cd test/output/
gitbslr checkout -f HEAD || true # parts of this will fail
cd ../../
mv test/output/.git/ test/.git/

tree test/output/ > test/output.log
tree test/repo/ | grep -v FAIL > test/expected.log
diff -U999 test/output.log test/expected.log

echo Test passed
