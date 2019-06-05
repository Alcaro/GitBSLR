#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script tests inlining of symlinks inside the repo, via GITBSLR_FOLLOW.


#input:
mkdir                   test/input/
mkdir                   test/input/sub1/
ln_sr test/input/sub1/  test/input/sub1/to_sub1
ln_sr test/input/       test/input/sub1/to_root
ln_sr test/input/sub2/  test/input/sub1/to_sub2
ln_sr test/input/sub2/  test/input/sub1/to_sub2_again
echo file1 >            test/input/sub1/file1

mkdir                   test/input/sub2/
ln_sr test/input/sub1/  test/input/sub2/to_sub1
ln_sr test/input/sub1/  test/input/sub2/to_sub1_again
ln_sr test/input/sub2/  test/input/sub2/to_sub2
echo file2 >            test/input/sub2/file2

mkdir                   test/input/sub3/
ln_sr test/input/sub1/  test/input/sub3/to_sub1
echo file3 >            test/input/sub3/file3

#ensure a /* inlines the indicated link too
mkdir                   test/input/sub4/
mkdir                   test/input/sub5/
echo file4 >            test/input/sub5/file4
ln_sr test/input/sub5/  test/input/sub4/to_sub5
ln_sr test/input/sub4/  test/input/to_sub4

export GITBSLR_FOLLOW="sub1/*:!sub1/to_sub2:$(pwd)/test/input/sub2/to_sub1/:to_sub4/*"


#expected output:
mkdir                                    test/expected/
mkdir                                    test/expected/sub1/
echo file1 >                             test/expected/sub1/file1
ln_sr test/expected/sub1/                test/expected/sub1/to_sub1
ln_sr test/expected/                     test/expected/sub1/to_root
ln_sr test/expected/sub2/                test/expected/sub1/to_sub2
mkdir                                    test/expected/sub1/to_sub2_again/
echo file2 >                             test/expected/sub1/to_sub2_again/file2
ln_sr test/expected/sub1/                test/expected/sub1/to_sub2_again/to_sub1
ln_sr test/expected/sub1/                test/expected/sub1/to_sub2_again/to_sub1_again
ln_sr test/expected/sub1/to_sub2_again/  test/expected/sub1/to_sub2_again/to_sub2

mkdir                                    test/expected/sub2/
echo file2 >                             test/expected/sub2/file2
mkdir                                    test/expected/sub2/to_sub1/
echo file1 >                             test/expected/sub2/to_sub1/file1
ln_sr test/expected/sub2/to_sub1/        test/expected/sub2/to_sub1/to_sub1
ln_sr test/expected/sub2/                test/expected/sub2/to_sub1/to_sub2
ln_sr test/expected/sub2/                test/expected/sub2/to_sub1/to_sub2_again
ln_sr test/expected/                     test/expected/sub2/to_sub1/to_root
ln_sr test/expected/sub1/                test/expected/sub2/to_sub1_again
ln_sr test/expected/sub2/                test/expected/sub2/to_sub2

mkdir                                    test/expected/sub3/
ln_sr test/expected/sub1/                test/expected/sub3/to_sub1
echo file3 >                             test/expected/sub3/file3

mkdir                                    test/expected/sub4/
mkdir                                    test/expected/sub5/
echo file4 >                             test/expected/sub5/file4
ln_sr test/expected/sub5/                test/expected/sub4/to_sub5
mkdir                                    test/expected/to_sub4/
mkdir                                    test/expected/to_sub4/to_sub5
echo file4 >                             test/expected/to_sub4/to_sub5/file4


cd test/input/
git init
gitbslr add .
git commit -m 'GitBSLR test'
cd ../../

mkdir test/output/
mv test/input/.git test/output/.git
cd test/output/
git reset --hard HEAD
cd ../../

tree test/output/ > test/output.log
tree test/expected/ > test/expected.log
diff -U999 test/output.log test/expected.log

echo Test passed
