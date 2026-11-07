#!/bin/sh
# Installs Aerie's desktop entry, metainfo and icons under PREFIX/share.
# This does not install the browser itself: put the aerie executable on PATH
# (or edit Exec= in the installed entry to its full path).
#
#   sh packaging/linux/install.sh ~/.local
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: install.sh PREFIX (for example ~/.local or /usr)" >&2
  exit 2
fi
prefix=$1
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
art="$here/../../branding/art/app_icon"
app_id=io.github.pedrosousa13.aerie

install -Dm644 "$here/aerie.desktop" "$prefix/share/applications/aerie.desktop"
install -Dm644 "$here/$app_id.metainfo.xml" "$prefix/share/metainfo/$app_id.metainfo.xml"
for size in 16 24 32 48 64 128 256; do
  install -Dm644 "$art/$size.png" "$prefix/share/icons/hicolor/${size}x${size}/apps/aerie.png"
done
echo "installed Aerie desktop files under $prefix/share"
