>>> This is a broken and abandoned branch; it is not suitable for general use <<<
>>> If you're good with OSX and willing to help, please do; if not, use the master branch <<<

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
    the loop point is treated as a symlink.
- If a file is accessible via multiple paths, you may end up with duplicate files in the repo. This
    may make 'git pull' annoying.
- Interaction with Git's cross-filesystem detector is untested.
- Interaction with submodules, --git-dir, and other rare features, is untested.
- Unix and Unix-likes only (only Linux tested), no native Windows support. Cygwin and WSL may work,
    but are untested. (Symlinks are rare on Windows anyways.)
- For security reasons, GitBSLR prevents Git from creating symlinks to outside the repo. See
    test2.sh for details.

To enable GitBSLR on your machine:
(1) Install a Unix-like operating system; only tested under Linux, but others may work (if not,
      report the bug)
(2) Install make and a C++11 compiler; only tested with GNU make and g++, but others will probably
      work (if not, report the bug)
(3) Compile GitBSLR with 'make', or 'make OPT=1' to enable my recommended optimizations, or 'make
      CFLAGS=-O3' if you want your own flags
(4) Add a wrapper script in your PATH that sets LD_PRELOAD=/path/to/gitbslr.so, then execs the real
      Git

install.sh will do steps 3 and 4 for you.

Configuration: GitBSLR obeys a few environment variables, which can be set per-invocation, or
    permanently in the wrapper script:
- GITBSLR_DEBUG
    If set, GitBSLR prints everything it does. If not, GitBSLR emits output only if it detects an
      error (i.e. Git trying to create symlinks to outside the repo, bad GitBSLR configuration, or a
      GitBSLR bug).
- GITBSLR_FOLLOW
    A colon-separated list of paths, as seen by Git, optionally prefixed with the absolute path to
      the repo.
    'path/link' or 'path/link/' will cause 'path/link' to be inlined. If path/link is nonexistent,
      not a symlink, or is outside the repo, the entry will be silently ignored.
    'path/link/*' will cause path/link/, and every symlink accessible under that path, to be
      inlined. '*' is a valid value and will cause exactly everything to be inlined.
    This applies only to paths as seen by Git; . and .. components are invalid, and symlinks inside
      paths are not followed.
    If the path is prefixed with !, it causes the non-inlining of an otherwise listed symlink. The
      last match applies, so paths should probably be in order from least to most specific.
    If using this, you may want to .gitignore the symlink target, to avoid duplicate files.
    To avoid infinite loops, symlinks that (after inlining) point to one of their in-repo parent
      directories will remain as symlinks. Additionally, if there are symlinks to one of the repo's
      parent directories, the repo root will be treated as a symlink.

GitBSLR will not automatically deduplicate anything, or otherwise create any symlinks for Git to
  follow. You have to create the symlinks yourself.

For security reasons, it's not recommended to enable GitBSLR non-globally; if vanilla Git creates a
  symlink to /home/username/ and GitBSLR follows it and creates a .bashrc, you would be quite
  disappointed. GitBSLR refuses to create symlinks to outside the repo, but vanilla Git can do it.
  This also applies to repositories cloned prior to installing GitBSLR; if you think they may be
  malicious, check for unexpected symlinks before using GitBSLR there, or delete and reclone. The
  GitBSLR configuration can safely be varied between repositories and invocations.
