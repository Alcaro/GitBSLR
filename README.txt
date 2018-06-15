GitBSLR - make Git follow symlinks

Making Git follow symlinks is a fairly common request <https://stackoverflow.com/questions/86402/how-can-i-get-git-to-follow-symlinks>.

But there's no real answer, only things that haven't worked since Git 1.6.1 (September 2010),
  hardlinks (requires root, and are increasingly rarely supported by filesystems), and other silly
  workarounds.

So I made a LD_PRELOAD-based tool to fix that. With this tool installed:
- Symlinks to anywhere inside the repo are still symlinks.
- Symlinks to outside the repo are treated as their contents.
- If inlining a symlink would yield a loop (for example symlinks to the repo's parent directory), the loop point is treated as a symlink.
- If multiple symlinks lead to the same outside-repo place, you may end up with duplicate files in the repo.
- Interaction with Git's cross-filesystem detector is untested.
- Interaction with submodules, --git-dir, and other rare features, is untested.
- Unix only (only Linux tested), no Windows support (but symlinks require root on Windows anyways).
- For security reasons, this device prevents Git from creating symlinks to outside the repo. See test2.sh for details.

To enable GitBSLR on your machine:
(1) Install a Unix-like operating system; only tested under Linux, but others will probably work (if not, report the bug)
(2) Install make and a C++11 compiler; only tested with GNU make and g++, but others will probably work (if not, report the bug)
(3) Compile GitBSLR with 'make', 'make OPT=1' to enable my recommended optimizations, or 'make CFLAGS=-O3' if you want your own flags
(4) Tell your shell to alias 'git' to 'LD_PRELOAD=/path/to/gitbslr.so git'

If you're using Bash, install.sh will do steps 3 and 4 for you. If you're using another shell,
  consult your documentation to find where to put aliases.

GitBSLR will not automatically deduplicate anything, or otherwise create any symlinks for Git to
  follow. You have to create the symlinks yourself.

For security reasons, it's not recommended to enable GitBSLR non-globally; if vanilla Git creates a
  symlink to /home/username/ and GitBSLR follows it and creates a .bashrc, you would be quite
  disappointed. GitBSLR refuses to create symlinks to outside the repo, but vanilla Git can do it.
  This also applies to repositories cloned prior to installing GitBSLR; if you think they may be
  malicious, check for unexpected symlinks before using GitBSLR there, or delete and reclone.
