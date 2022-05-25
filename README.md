GitBSLR - make Git follow symlinks
========

Making Git follow symlinks is a fairly common request <https://stackoverflow.com/questions/86402/how-can-i-get-git-to-follow-symlinks>.

But there's no real answer, only things that haven't worked since Git 1.6.1 (September 2010), hardlinks (requires root, and are increasingly rarely supported by filesystems), and other silly workarounds.

So I made a LD_PRELOAD-based tool to fix that. With this tool installed, symlinks to outside the repo are treated as their contents. (To avoid duplicate files, symlinks to anywhere inside the repo are still symlinks.)

Things that don't work, or aren't tested:
- If someone checked in a symlink to outside the repo, GitBSLR will refuse to clone it. This is for security reasons; if vanilla Git creates a symlink to /home/username/, and GitBSLR follows it and creates a .bashrc, you would be quite disappointed. This also applies to repositories cloned prior to installing GitBSLR; if you think they may contain inappropriate links, check them before using GitBSLR, or delete and reclone.
- Interaction with rarer Git features, like rebase or the cross-filesystem detector, is untested. If you think it should work, submit a PR or issue. Please include complete steps to reproduce, I don't know much about Git.
- Anything complex (links to links, links within links, links to nonexistent files, etc) may yield unexpected results. (If sufficiently complex, it's not even clear what behavior would be expected.)
- GitBSLR is only tested on Linux. Other Unix-likes may work, but are untested; feel free to try. For Windows, WSL or Cygwin will probably work (though symlinks are rare on Windows).
- GitBSLR is only tested with glibc. Other libcs may work, but I've had a few bugs around glibc upgrades, so no promises.
- --work-tree, --git-dir and similar don't work; GitBSLR can't see command line arguments, and will be confused. Use the GITBSLR_GIT_DIR and GITBSLR_WORK_TREE environment variables instead.
- Performance is not a goal of GitBSLR; I haven't noticed any slowdown, but I also haven't used GitBSLR on any large repos where performance is relevant. If it's too slow for you, the best solution is to petition upstream Git to add this functionality.

To enable GitBSLR on your machine:
1. Install your favorite Linux distro (or other Unix-like environment, if you're feeling lucky)
2. Install make and a C++ compiler; only tested with GNU make and g++, but others will probably work (if not, report the bug)
3. Compile GitBSLR with 'make', or 'make OPT=1' to enable my recommended optimizations, or 'make CFLAGS=-O3 LFLAGS=-s' if you want your own flags
4. Run GitBSLR's test suite, with 'make test'; GitBSLR makes many guesses about implementation details of Git and libc, and may yield subtle breakage or security holes if it guesses wrong
5. Add a wrapper script in your PATH that sets LD_PRELOAD=/path/to/gitbslr.so, then execs the real Git

install.sh will do steps 3 to 5 for you, but not 1 or 2.

Configuration: GitBSLR obeys a few environment variables, which can be set per-invocation, or permanently in the wrapper script:
- GITBSLR_DEBUG
If set, GitBSLR prints everything it does. If not, GitBSLR emits output only if it's unable to continue (for example Git trying to create symlinks to outside the repo, bad GitBSLR configuration, or a GitBSLR bug).
- GITBSLR_FOLLOW
A colon-separated list of paths, as seen by Git, optionally prefixed with the absolute path to the repo.
'path/link' or 'path/link/' will cause 'path/link' to be inlined. If path/link is nonexistent, not a symlink, or is outside the repo, the entry will be silently ignored.
'path/link/*' will cause path/link/, and every symlink accessible under that path, to be inlined.
'*' alone is a valid value and will cause exactly everything to be inlined. This applies only to paths as seen by Git; . and .. components are invalid, and symlinks inside paths are not followed.
If the path is prefixed with !, it causes the non-inlining of an otherwise listed symlink.
The last match applies, so paths should be in order from least to most specific.
If using this, you most likely want to .gitignore the symlink target, to avoid duplicate files.
To avoid infinite loops, symlinks that (after inlining) point to one of their in-repo parent directories will remain as symlinks. Additionally, if there are symlinks to one of the repo's parent directories, the repo root will be treated as a symlink.
- GITBSLR_GIT_DIR
By default, GitBSLR assumes the Git directory is the first existing accessed path containing a .git component. If yours is elsewhere, you can override this default.
Note that GitBSLR does not use the GIT_DIR variable. This is since there are three ways to set this path: GIT_DIR=, --git-dir=, and defaulting to the closest .git in the working directory.
For architectural reasons, GitBSLR cannot access the command line arguments, and having two of three ways functional would be misleading. Better obviously dumb than giving people bad expectations.
If this is set, GitBSLR will set GIT_DIR for you. However, --git-dir overrides GIT_DIR, so don't use that.
WARNING: Setting this variable incorrectly, or not setting it if it should be set, is very likely to yield security holes or other trouble.
- GITBSLR_WORK_TREE
By default, GitBSLR assumes the work tree is the parent of the Git directory. If yours is elsewhere, you can override this default.
Note that GitBSLR does not use the GIT_WORK_TREE variable. This is since there are four ways to set this path: GIT_DIR=, --git-dir=, .git/config, and defaulting to GIT_DIR's parent. Like GIT_DIR, some of those are unavailable to GitBSLR; better obviously dumb than almost smart enough.
If this is set, GitBSLR will set GIT_WORK_TREE for you. However, --work-tree overrides GIT_WORK_TREE, so don't use that.
WARNING: Setting this variable incorrectly, or not setting it if it should be set, is very likely to yield security holes or other trouble.

GitBSLR will not automatically deduplicate anything, or otherwise create any symlinks for Git to follow. You have to create the symlinks yourself.
