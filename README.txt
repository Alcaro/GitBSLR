GitBSLR - make Git follow symlinks

Making Git follow symlinks is a fairly common request <https://stackoverflow.com/questions/86402/how-can-i-get-git-to-follow-symlinks>.

But there's no real answer, only things that haven't worked since Git 1.6.1 (September 2010),
  hardlinks (requires root, and are increasingly rarely supported by filesystems), and other silly
  workarounds.

So I made a LD_PRELOAD-based tool to fix that. With this tool installed:
- Symlinks to anywhere inside the repo are still symlinks.
- Symlinks to outside the repo are treated as their contents.
- If inlining a symlink would yield a loop (for example symlinks to the repo's parent directory), the loop point is treated as a symlink.
- If multiple symlinks lead to the same outside-repo place, you may end up with duplicate files.
- Interaction with Git's cross-filesystem detector is untested.
- Interaction with submodules, and other rare features, is untested.
- Unix only (only Linux tested), no Windows support (but symlinks require root on Windows anyways - a junction probably works better).
- For security reasons, this device prevents Git from creating symlinks to outside the repo. See test2.sh for details.
