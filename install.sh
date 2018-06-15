#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# GitBSLR is available under the same license as Git itself.

make OPT=1 || exit $?
GITBSLR=$(readlink -f $(dirname $0))/gitbslr.so
#TODO: figure out how much escaping needs to be done to make this append to LD_PRELOAD, rather than overwrite it
#TODO: figure out if LD_PRELOAD even supports preloading multiple things
#TODO: if doing this, fix the unsetenv call too
NEWLINE='alias git="LD_PRELOAD='$GITBSLR' git"'
grep -q -F $GITBSLR ~/.bashrc || echo $NEWLINE >> ~/.bashrc
echo "Installed for user $USER under Bash, restart your shell or reload your ~/.bashrc"
