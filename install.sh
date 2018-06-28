#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

make OPT=1 || exit $?

[ -d ~/bin/ ] || mkdir ~/bin/
[ -e ~/bin/git ] && rm ~/bin/git

cp $(readlink -f $(dirname $0))/gitbslr.so ~/bin/
GITORIG=$(which git)

#TODO: make this append to LD_PRELOAD if one is already set
cat > ~/bin/git << EOF
#!/bin/sh
export LD_PRELOAD=$HOME/bin/gitbslr.so
exec $GITORIG "\$@"
EOF

chmod +x ~/bin/git
chmod -x ~/bin/gitbslr.so

if [ "$(which git)" != ~/bin/git ]; then
echo "~/bin/ is not in your PATH; fix that to complete the installation"
else
echo "Installed for user $USER"
fi
