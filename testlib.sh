#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

#This script contains various helper functions needed by all of GitBSLR's tests.

#dash doesn't support pipefail
set -eu

make
rm -rf test/
[ -e test/ ] && exit 1
mkdir test/

GIT=/usr/bin/git
git()
{
  >&2 echo git "$@"
  $GIT "$@"
}
GITBSLR=$(pwd)/gitbslr.so
gitbslr()
{
  >&2 echo gitbslr "$@"
  LD_PRELOAD=$GITBSLR $GIT "$@"
}
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
  find $1 -printf '%P -> %l\n' | grep -v .git | LC_ALL=C sort ||
  perl -e '
    chdir $ARGV[0];
    use File::Find qw(finddepth);
    finddepth(sub {
      print $File::Find::name, " -> ", (readlink $_ or ""), "\n";
    }, ".");
    ' $1 | grep -v .git | LC_ALL=C sort
}
