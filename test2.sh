#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

#dash doesn't support pipefail
set -eu

cd $(dirname $0)
make || exit $?
rm -rf test/ || exit $?
[ -e test/ ] && exit 1
mkdir test/ || exit $?

GIT=/usr/bin/git
git()
{
  $GIT "$@"
}
case $(uname -s) in
  Darwin*)
    #why must you claim to be unix-like, yet be so different
    GITBSLR=$(pwd)/gitbslr.dylib
    gitbslr()
    {
      DYLD_INSERT_LIBRARIES=$GITBSLR $GIT "$@"
    }
  ;;
  *)
    GITBSLR=$(pwd)/gitbslr.so
    gitbslr()
    {
      LD_PRELOAD=$GITBSLR $GIT "$@"
    }
  ;;
esac
export GITBSLR_DEBUG=1

ln_sr()
{
  #Perl is no beauty, but anything else I could find requires Bash, or other programs not guaranteed to exist
  ln -sr $1 $2 || perl -e'use File::Spec; use File::Basename;
                          symlink File::Spec->abs2rel($ARGV[0], dirname($ARGV[1])), $ARGV[1] or
                              die qq{cannot create symlink: $!$/}' $1 $2
}

tree()
{
  perl -e '
    use File::Find qw(finddepth);
    my @files;
    finddepth(sub {
      print $File::Find::name, " -> ", readlink($File::Find::name), "\n";
    }, $ARGV[0]);
    ' $1 | sed s%$1%% | grep -v .git | LC_ALL=C sort
}

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
