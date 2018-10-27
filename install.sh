#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

#dash doesn't support pipefail
set -eu

[ -e gitbslr.so ] || make OPT=1 || exit $?

TARGET="$HOME/bin"
[ $(id -u) -eq 0 ] && TARGET="/usr/local/bin"

[ -d $TARGET ] || mkdir $TARGET
if [ -e $TARGET/git ]; then
  if grep -q gitbslr.so $TARGET/git; then
    rm $TARGET/git
    rm $TARGET/gitbslr.so
  else
    echo "error: $TARGET/git exists and isn't GitBSLR, not going to overwrite that"
    exit 1
  fi
fi

if [ "x${1:-x}" = "xuninstall" ]; then
  echo "Uninstalled GitBSLR from $TARGET/git"
  exit 0
fi

GITORIG=$(which git)
if [ x"$GITORIG" = x ]; then
  echo "error: you need to install Git before you can install GitBSLR"
  exit 1
fi
cp $(readlink -f $(dirname $0))/gitbslr.so $TARGET/gitbslr.so

#TODO: make this append to LD_PRELOAD if one is already set
#(also requires making the initialization unsetenv remove GitBSLR only)
cat > $TARGET/git << EOF
#!/bin/sh
export LD_PRELOAD=$TARGET/gitbslr.so
exec $GITORIG "\$@"
EOF

chmod +x $TARGET/git
chmod -x $TARGET/gitbslr.so

if [ "$(which git)" != $TARGET/git ]; then
  echo "warning: installed to $TARGET/git, but $TARGET/ is not in your PATH (or another Git is in front of $TARGET/); fix that to complete the installation"
elif [ "$(which git)" = /usr/local/git ]; then
  echo "Installed GitBSLR to $TARGET/git"
fi
