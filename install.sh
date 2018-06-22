#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# GitBSLR is available under the same license as Git itself.

make OPT=1 || exit $?
GITBSLR=$(readlink -f $(dirname $0))/gitbslr.so
[ -d ~/bin/ ] || mkdir ~/bin/
[ -e ~/bin/git ] && rm ~/bin/git
GITORIG=$(which git)
cat > ~/bin/git << EOF
#!/bin/sh
export LD_PRELOAD=$GITBSLR
exec $GITORIG "\$@"
EOF
chmod +x ~/bin/git
if [ "$(which git)" != ~/bin/git ]; then
echo "~/bin/ is not in your PATH; fix that to complete the installation"
else
echo "Installed for user $USER"
fi
