#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

#dash doesn't support pipefail
set -eu

[ -e gitbslr.so ] || make OPT=1 || exit $?

TARGET="$HOME/bin"
[ $(id -u) -eq 0 ] && TARGET="/usr/local/bin"

CREATED_TARGET=0

if [ ! -d $TARGET ]; then
  mkdir TARGET
  CREATED_TARGET=1
fi
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
  case ":$PATH:" in
  *:$TARGET:*)
    echo "warning: installed to $TARGET/git, but another Git is already in your \$PATH, in front of $TARGET/; fix that to complete the installation"
    ;;
  *:$TARGET/:*)
    # can't find a way to deduplicate these branches
    echo "warning: installed to $TARGET/git, but another Git is already in your \$PATH, in front of $TARGET/; fix that to complete the installation"
    ;;
  *)
    # don't source .profile directly, it's incompatible with set -u
    if [ $CREATED_TARGET = 1 ] && [ $(sh -c ". ~/.profile; which git") = $TARGET/git ]; then
      echo "Installed GitBSLR to $TARGET/git"
      echo "To complete installation, run"
      echo "  export PATH=\"\$HOME/bin:\$PATH\""
      echo "in all terminals, or restart your login session or computer"
    elif [ TARGET = "$HOME/bin" ]; then
      echo "Installed GitBSLR to $TARGET/git; run"
      echo "  echo 'export PATH=\"\$HOME/bin:\$PATH\"' >> ~/.profile"
      echo "and restart your computer to complete the installation"
    else
      echo "warning: installed GitBSLR to $TARGET/git, but $TARGET/ is not in your PATH; fix that to complete the installation"
    fi
    ;;
  esac
else
  echo "Installed GitBSLR to $TARGET/git"
fi
echo "To verify whether GitBSLR is correctly installed, run"
echo "  GITBSLR_DEBUG=1 git version"
echo "and check if it says \"GitBSLR: Loaded\"."

echo "You should also run GitBSLR's test suite, with"
echo "  make test"
echo "to ensure GitBSLR works on your platform."
