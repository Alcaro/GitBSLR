#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script tests some rarer stuff, like ls-files and symlinks to symlinks.

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

#TODO: this is a bug, but its impact is minuscle, and it would require some pretty complex tricks to untangle
## repo/a -> repo/b -> not_repo/c - should inline b, keep a as link
#echo test >                     test/outfile
#ln_sr test/outfile              test/repo/to_outfile
#echo test >                     test/expected/to_outfile
#ln_sr test/repo/to_outfile      test/repo/to_to_outfile
#ln_sr test/expected/to_outfile  test/expected/to_to_outfile

# repo/a -> not_repo/b -> repo/c - should inline a once, leaving it as link to c
ln_sr test/repo/file            test/file_detour
ln_sr test/file_detour          test/repo/to_to_file_detour
ln_sr test/expected/file        test/expected/to_to_file_detour

#TODO: this is a bug, but I can't find how to fix it; it'd require finding the best place where not_repo/b/../repo is mapped in the repo
#while one place is guaranteed to exist, links behind a GITBSLR_FOLLOW (with the original ignored) shouldn't point to the 'real' path
## repo/a -> not_repo/b; not_repo/b/c -> repo/d - should inline a, and leave c as link, while ensuring it still points where it should
#mkdir                           test/dir
#ln_sr test/dir                  test/repo/to_dir
#mkdir                           test/expected/to_dir
#ln_sr test/repo/file            test/dir/to_file
#ln_sr test/expected/file        test/expected/to_dir/to_file

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
