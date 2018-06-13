#!/bin/bash

#set -v

cd $(dirname $0)
make || exit $?
rm -rf test/ || exit $?
[ -e test/ ] && exit 1
mkdir test/ || exit $?


#input:
mkdir                                    test/input/
mkdir                                    test/input/wrap/
mkdir                                    test/input/wrap/the_repo/
mkdir                                    test/input/wrap/the_repo/subdir1/
echo file1 >                             test/input/wrap/the_repo/subdir1/file1
mkdir                                    test/input/wrap/the_repo/subdir2/
echo file2 >                             test/input/wrap/the_repo/subdir2/file2
echo file3 >                             test/input/wrap/the_repo/file3
ln -sr test/input/wrap/                  test/input/wrap/the_repo/to_outside
ln -sr test/input/wrap/                  test/input/wrap/the_repo/to_outside2
ln -s  /bin/sh                           test/input/wrap/the_repo/to_bin_sh
ln -sr test/input/wrap/the_repo/         test/input/wrap/the_repo/to_root
ln -sr test/input/wrap/the_repo/file3    test/input/wrap/the_repo/to_file3
ln -sr test/input/wrap/the_repo/subdir1/ test/input/wrap/the_repo/to_subdir1
ln -sr test/input/wrap/                  test/input/wrap/to_outside_again
ln -sr test/input/                       test/input/wrap/further_outside
mkdir                                    test/input/wrap/subdir3/
echo file5 >                             test/input/wrap/subdir3/file5
echo file4 >                             test/input/wrap/file4


#under the careful delusions of GitBSLR, Git will see this as:
mkdir                            test/expected/
mkdir                            test/expected/subdir1/
echo file1 >                     test/expected/subdir1/file1
mkdir                            test/expected/subdir2/
echo file2 >                     test/expected/subdir2/file2
echo file3 >                     test/expected/file3
ln -sr test/expected/            test/expected/to_root
mkdir                            test/expected/to_outside/
ln -sr test/expected/            test/expected/to_outside/the_repo
ln -sr test/expected/to_outside  test/expected/to_outside/to_outside_again
mkdir                            test/expected/to_outside/further_outside
ln -sr test/expected/to_outside  test/expected/to_outside/further_outside/wrap
echo file4 >                     test/expected/to_outside/file4
mkdir                            test/expected/to_outside/subdir3/
echo file5 >                     test/expected/to_outside/subdir3/file5
mkdir                            test/expected/to_outside2/
#yes, duplicate; neither to_outside nor to_outside2 is subordinate to the other, so GitBSLR inlines both rather than linking them
ln -sr test/expected/            test/expected/to_outside2/the_repo
ln -sr test/expected/to_outside2 test/expected/to_outside2/to_outside_again
mkdir                            test/expected/to_outside2/further_outside
ln -sr test/expected/to_outside2 test/expected/to_outside2/further_outside/wrap
echo file4 >                     test/expected/to_outside2/file4
mkdir                            test/expected/to_outside2/subdir3/
echo file5 >                     test/expected/to_outside2/subdir3/file5
#Change these two lines if you choose to not inline absolute symlinks.
#ln -s  /bin/sh                   test/expected/to_bin_sh
cp     /bin/sh                   test/expected/to_bin_sh
ln -sr test/expected/file3       test/expected/to_file3
ln -sr test/expected/subdir1     test/expected/to_subdir1


cd test/input/wrap/the_repo/
git init
#strace -E LD_PRELOAD=../../../gitbslr.so  git add . 2>&1 | tee ../../../e.log
#strace git add . 2>&1 | tee ../../../e.log
LD_PRELOAD=../../../../gitbslr.so git add . || exit $?
git commit -m "GitBSLR test"
cd ../../../../

mkdir test/output/
mv test/input/wrap/the_repo/.git test/output/.git
cd test/output/
LD_PRELOAD= git reset --hard HEAD
cd ../../

cd test/output/
find -printf '%p -> %l\n' | grep -v .git | sort > ../output.log
cd ../../
cd test/expected/
find -printf '%p -> %l\n' | grep -v .git | sort > ../expected.log
cd ../../

diff -u999 test/output.log test/expected.log && echo Test passed; exit $?
