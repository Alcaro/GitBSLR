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
      DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=$GITBSLR $GIT "$@"
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


#input:
mkdir                                    test/input/
mkdir                                    test/input/wrap/
mkdir                                    test/input/wrap/the_repo/
mkdir                                    test/input/wrap/the_repo/subdir1/
echo file1 >                             test/input/wrap/the_repo/subdir1/file1
mkdir                                    test/input/wrap/the_repo/subdir2/
echo file2 >                             test/input/wrap/the_repo/subdir2/file2
echo file3 >                             test/input/wrap/the_repo/file3
ln_sr test/input/wrap/                   test/input/wrap/the_repo/to_outside
ln_sr test/input/wrap/                   test/input/wrap/the_repo/to_outside2
ln -s /bin/sh                            test/input/wrap/the_repo/to_bin_sh
ln_sr test/input/wrap/the_repo/          test/input/wrap/the_repo/to_root
ln_sr test/input/wrap/the_repo/file3     test/input/wrap/the_repo/to_file3
ln_sr test/input/wrap/the_repo/subdir1/  test/input/wrap/the_repo/to_subdir1
ln_sr test/input/wrap/                   test/input/wrap/to_outside_again
ln_sr test/input/                        test/input/wrap/further_outside
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
ln_sr test/expected/             test/expected/to_root
mkdir                            test/expected/to_outside/
ln_sr test/expected/             test/expected/to_outside/the_repo
ln_sr test/expected/to_outside   test/expected/to_outside/to_outside_again
mkdir                            test/expected/to_outside/further_outside
ln_sr test/expected/to_outside   test/expected/to_outside/further_outside/wrap
echo file4 >                     test/expected/to_outside/file4
mkdir                            test/expected/to_outside/subdir3/
echo file5 >                     test/expected/to_outside/subdir3/file5
mkdir                            test/expected/to_outside2/
#yes, duplicate; neither to_outside nor to_outside2 is subordinate to the other,
# so GitBSLR doesn't know which to make a link, and instead inlines both
ln_sr test/expected/             test/expected/to_outside2/the_repo
ln_sr test/expected/to_outside2  test/expected/to_outside2/to_outside_again
mkdir                            test/expected/to_outside2/further_outside
ln_sr test/expected/to_outside2  test/expected/to_outside2/further_outside/wrap
echo file4 >                     test/expected/to_outside2/file4
mkdir                            test/expected/to_outside2/subdir3/
echo file5 >                     test/expected/to_outside2/subdir3/file5
cp    /bin/sh                    test/expected/to_bin_sh
ln_sr test/expected/file3        test/expected/to_file3
ln_sr test/expected/subdir1      test/expected/to_subdir1

cd test/input/wrap/the_repo/
gitbslr init
grep -q 'symlinks = false' .git/config && echo Error: No symlink support
grep -q 'symlinks = false' .git/config && exit 1
gitbslr add . || exit $?
#this could simply be
#git commit -m "GitBSLR test" || exit $?
#but I want this to ensure https://github.com/Alcaro/GitBSLR/issues/1 doesn't regress
export EDITOR=../../../../test-dummyeditor.pl
gitbslr commit || exit $?
cd ../../../../

mkdir test/output/
mv test/input/wrap/the_repo/.git test/output/.git
cd test/output/
#not gitbslr here, we want to extract what Git actually saw
git reset --hard HEAD
cd ../../

tree test/output > test/output.log
tree test/expected > test/expected.log
diff -U999 test/output.log test/expected.log && echo Test passed; exit $?
