#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script tests what happens if cwd is a subdirectory of the repo.

mkdir                      test/repo
mkdir                      test/repo/dir
echo test >                test/repo/dir/file
mkdir                      test/repo/dir2
echo test2 >               test/repo/dir2/file2
mkdir                      test/not_repo
echo test3 >               test/not_repo/file3
ln_sr test/not_repo        test/repo/dir2/another_dir
ln_sr test/repo/dir2/file2 test/not_repo/another_link

mkdir                      test/expected
cd test/repo
gitbslr init
gitbslr add .
gitbslr commit -m test
cd ../..
mv test/repo/.git          test/expected/.git
cd test/expected
gitbslr reset --hard
cd ../..

cd test/repo
gitbslr init
cd dir
gitbslr add ..
gitbslr commit -m test
cd ../../..
mkdir                      test/actual1
mv       test/repo/.git    test/actual1/.git
cd test/actual1
gitbslr reset --hard
cd ../..

mkdir                      test/actual2
mkdir                      test/actual2/dir2
mv    test/actual1/.git    test/actual2/.git
cd test/actual2/dir2
gitbslr reset --hard
cd ../../..

tree test/expected/ > test/expected.log
tree test/actual1/ > test/actual1.log
tree test/actual2/ > test/actual2.log
diff -U999 test/actual1.log test/expected.log
diff -U999 test/actual2.log test/expected.log

echo Test passed
