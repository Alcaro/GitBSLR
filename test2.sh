#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

cd $(dirname $0)
. ./testlib.sh

#This script ensures GitBSLR can't create symlinks to outside the repository.

#With GitBSLR installed, Git can end up writing to outside the repository directory. If a pulled
# repository is malicious, this can cause remote code execution, for example by scribbling across your .bashrc.
#GitBSLR must prevent that. Since the entire point of GitBSLR is writing outside the repo, something
# else must be changed. The only available option is preventing Git from creating symlinks to outside the repo root.
#The simplest and most effective way would be returning an error. The most appropriate one would be EPERM,
# "The filesystem containing linkpath does not support the creation of symbolic links."
#(Git claims to support filesystems not supporting symlinks, but that just replaces the links with
# plaintext files containing their target, aka silently corrupting the tree. Better tell Git we
# support links, causing it to show the unexpected error to the user.)


mkdir test/victim/
echo echo Test passed > test/victim/script.sh
mkdir test/evilrepo_v1/

cd test/evilrepo_v1/
git init
ln -s ../victim/ evil_symlink
git add .
git commit -m "GitBSLR test part 1"
cd ../..

mkdir test/evilrepo_v2/
cd test/evilrepo_v2/
git init
mkdir evil_symlink/
echo echo Installing Bitcoin miner... > evil_symlink/script.sh
git add .
git commit -m "GitBSLR test part 2"
cd ../..

mkdir test/clone/
cd test/clone/
mv ../evilrepo_v1/.git ./.git
gitbslr reset --hard || true # supposed to fail, shouldn't hit -e
mv .git ../evilrepo_v1/.git
mv ../evilrepo_v2/.git ./.git
gitbslr reset --hard
mv .git ../evilrepo_v2/.git

cd ../../
sh test/victim/script.sh
