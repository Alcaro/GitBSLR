GitBSLR - make Git follow symlinks

Making Git follow symlinks is a fairly common request
  <https://stackoverflow.com/questions/86402/how-can-i-get-git-to-follow-symlinks>.

But there's no real answer, only things that haven't worked since Git 1.6.1 (September 2010),
  hardlinks (requires root, and are increasingly rarely supported by filesystems), and other silly
  workarounds.

So I made a LD_PRELOAD-based tool to fix that. With this tool installed:
- Symlinks to outside the repo are treated as their contents.
- To avoid duplicate files, symlinks to anywhere inside the repo are still symlinks. If you want to
    follow all links, use GITBSLR_FOLLOW.
- If inlining a symlink would yield a loop (for example symlinks to the repo's parent directory),
    the loop point becomes a symlink, overriding GITBSLR_FOLLOW.
- If a file is accessible via multiple paths, you may end up with duplicate files in the repo. This
    may make 'git pull' annoying.

Things that don't work, or aren't tested:
- If someone checked in a symlink to outside the repo, things won't work. See test2.sh for details.
- Interaction with rare Git features, like submodules or the cross-filesystem detector, is untested.
- Anything complex (links to links, links within links, broken links, etc) may yield weird results.
- GitBSLR does not work natively on Windows or OSX. (It may work in WSL and Cygwin - not tested.)
- GitBSLR is only tested on Linux, though I'd expect it to work on most other Unix-likes.
- GitBSLR is only tested with glibc, though I'd expect it to work on most libcs.
- --work-tree, --git-dir and similar don't work. Use GITBSLR_GIT_DIR and GITBSLR_WORK_TREE instead.

To enable GitBSLR on your machine:
(1) Install your favorite Linux flavor (or other Unix-like environment, if you're feeling lucky)
(2) Install make and a C++ compiler; only tested with GNU make and g++, but others will probably
      work (if not, report the bug)
(3) Compile GitBSLR with 'make', or 'make OPT=1' to enable my recommended optimizations, or 'make
      CFLAGS=-O3 LFLAGS=-s' if you want your own flags
(4) Add a wrapper script in your PATH that sets LD_PRELOAD=/path/to/gitbslr.so, then execs the real
      Git

install.sh will do steps 3 and 4 for you.

Configuration: GitBSLR obeys a few environment variables, which can be set per-invocation, or
    permanently in the wrapper script:
- GITBSLR_DEBUG
    If set, GitBSLR prints everything it does. If not, GitBSLR emits output only if it's unable to
      continue (Git trying to create symlinks to outside the repo, bad GitBSLR configuration, or a
      GitBSLR bug).
- GITBSLR_FOLLOW
    A colon-separated list of paths, as seen by Git, optionally prefixed with the absolute path to
      the repo.
    'path/link' or 'path/link/' will cause 'path/link' to be inlined. If path/link is nonexistent,
      not a symlink, or is outside the repo, the entry will be silently ignored.
    'path/link/*' will cause path/link/, and every symlink accessible under that path, to be
      inlined. '*' alone is a valid value and will cause exactly everything to be inlined.
    This applies only to paths as seen by Git; . and .. components are invalid, and symlinks inside
      paths are not followed.
    If the path is prefixed with !, it causes the non-inlining of an otherwise listed symlink. The
      last match applies, so paths should be in order from least to most specific.
    If using this, you most likely want to .gitignore the symlink target, to avoid duplicate files.
    To avoid infinite loops, symlinks that (after inlining) point to one of their in-repo parent
      directories will remain as symlinks. Additionally, if there are symlinks to one of the repo's
      parent directories, the repo root will be treated as a symlink.
- GITBSLR_GIT_DIR
    By default, GitBSLR assumes the Git directory is the first existing accessed path containing a
      .git component. If yours is elsewhere, you can override this default.
    Note that GitBSLR does not use the GIT_DIR variable. This is since there are three ways to set
      this path: GIT_DIR=, --git-dir=, and defaulting to the closest .git in the working directory.
      For architectural reasons, GitBSLR cannot access the command line arguments, and having two of
      three ways functional would be misleading. Better obviously dumb than giving people bad
      expectations.
    If this is set, GitBSLR will set GIT_DIR for you. However, --git-dir overrides GIT_DIR, so don't
      use that.
    WARNING: Setting this variable incorrectly, or not setting it if it should be set, is very
      likely to yield security holes or other trouble.
- GITBSLR_WORK_TREE
    By default, GitBSLR assumes the work tree is the parent of the Git directory. If yours is
      elsewhere, you can override this default.
    Note that GitBSLR does not use the GIT_WORK_TREE variable. This is since there are four ways to
      set this path: GIT_DIR=, --git-dir=, .git/config, and defaulting to GIT_DIR's parent. Like
      GIT_DIR, some of those are unavailable to GitBSLR; better obviously dumb than almost smart
      enough.
    If this is set, GitBSLR will set GIT_WORK_TREE for you. However, --work-tree overrides
      GIT_WORK_TREE, so don't use that.
    WARNING: Setting this variable incorrectly, or not setting it if it should be set, is very
      likely to yield security holes or other trouble.

GitBSLR will not automatically deduplicate anything, or otherwise create any symlinks for Git to
  follow. You have to create the symlinks yourself.

For security reasons, it's not recommended to enable GitBSLR non-globally; if vanilla Git creates a
  symlink to /home/username/, and GitBSLR follows it and creates a .bashrc, you would be quite
  disappointed. GitBSLR refuses to create symlinks to outside the repo, but vanilla Git can do it.
  This also applies to repositories cloned prior to installing GitBSLR; if you think they may
  contain inappropriate links, check them before using GitBSLR there, or delete and reclone. The
  GitBSLR configuration can safely be varied between repositories and invocations.
