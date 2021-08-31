#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script tests some rarer stuff, like ls-files, symlinks to symlinks, and symlinks to absolute paths in the repo.

# ls-files
mkdir                                    test/repo/
mkdir                                    test/repo/dir/
echo test >                              test/repo/dir/file
ln_sr test/repo/dir/                     test/repo/ln_dir

echo dir/file     >> test/expected.log
echo ln_dir       >> test/expected.log

cd test/repo/
gitbslr init
gitbslr ls-files --others --exclude-standard > ../output.log
cd ../../

diff -U999 test/output.log test/expected.log || exit $?
rm -rf test/*


# Symlinks to symlinks
mkdir                           test/repo/
mkdir                           test/expected/

# repo/a -> repo/b -> repo/c -  should let a point to b
echo test >                     test/repo/file
echo test >                     test/expected/file
ln_sr test/repo/file            test/repo/to_file
ln_sr test/expected/file        test/expected/to_file
ln_sr test/repo/to_file         test/repo/to_to_file
ln_sr test/expected/to_file     test/expected/to_to_file

# repo/a -> not_repo/b -> repo/c - should inline a once, leaving it as link to c
ln_sr test/repo/file            test/file_detour
ln_sr test/file_detour          test/repo/to_to_file_detour
ln_sr test/expected/file        test/expected/to_to_file_detour

# repo/a -> not_repo/b; not_repo/b/c -> repo/d - should inline a, and leave c as link, while ensuring it still points where it should
mkdir                           test/dir
ln_sr test/dir                  test/repo/to_dir
mkdir                           test/expected/to_dir
ln_sr test/repo/file            test/dir/to_file
ln_sr test/expected/file        test/expected/to_dir/to_file

# the above, but with an extra subdirectory
# repo/a -> not_repo/b; not_repo/b/c -> repo/d - should inline a, and leave c as link, while ensuring it still points where it should
mkdir                           test/repo/sub
mkdir                           test/expected/sub
echo test >                     test/repo/sub/file
echo test >                     test/expected/sub/file
mkdir                           test/subdir
ln_sr test/subdir               test/repo/sub/to_subdir
mkdir                           test/expected/sub/to_subdir
ln_sr test/repo/sub/file        test/subdir/to_file
ln_sr test/expected/sub/file    test/expected/sub/to_subdir/to_file

# absolute symlinks into repo
mkdir                           test/repo/sub2/
mkdir                           test/expected/sub2/
echo test >                     test/repo/sub2/file2
echo test >                     test/expected/sub2/file2
ln -s $(realpath test/repo/sub2/) test/repo/to_sub2
ln_sr test/expected/sub2/       test/expected/to_sub2
ln -s $(realpath test/repo/sub2/) test/repo/sub/to_sub2
ln_sr test/expected/sub2/       test/expected/sub/to_sub2
ln -s $(realpath test/repo/sub/file) test/repo/sub/to_subfile
ln_sr test/expected/sub/file    test/expected/sub/to_subfile

#TODO: figure out what to do with links to nonexistent in-repo, or out-of-repo, targets




cd test/repo/
gitbslr init
gitbslr add .
gitbslr commit -m "GitBSLR test"
cd ../../

mkdir test/output/
mv test/repo/.git test/output/.git
cd test/output/
#not gitbslr here, we want to extract what Git actually saw
git reset --hard HEAD
cd ../../


tree test/output/ > test/output.log
tree test/expected/ > test/expected.log
diff -U999 test/output.log test/expected.log

echo Test passed
