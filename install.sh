#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

make OPT=1 || exit $?

[ -d ~/bin/ ] || mkdir ~/bin/
if [ -e ~/bin/git ]; then
  if grep -q gitbslr.so ~/bin/git; then
    rm ~/bin/git
  else
    echo "error: ~/bin/git exists and isn't GitBSLR, not going to overwrite that"
    exit 1
  fi
fi

GITORIG=$(which git)
if [ x"$GITORIG" = x ]; then
  echo "error: you need to install Git before you can install GitBSLR"
  exit 1
fi
cp $(readlink -f $(dirname $0))/gitbslr.so ~/bin/gitbslr.so

#TODO: make this append to LD_PRELOAD if one is already set
cat > ~/bin/git << EOF
#!/bin/sh
export LD_PRELOAD=$HOME/bin/gitbslr.so
exec $GITORIG "\$@"
EOF

chmod +x ~/bin/git
chmod -x ~/bin/gitbslr.so

if [ "$(which git)" != ~/bin/git ]; then
  echo "warning: installed to ~/bin/git, but ~/bin/ is not in your PATH (or another Git is in front of ~/bin/); fix that to complete the installation"
  exit 1
else
  echo "Installed for user $USER"
fi
