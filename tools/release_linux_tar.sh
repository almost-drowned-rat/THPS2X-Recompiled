#!/usr/bin/env bash
# release_linux_tar.sh - the Linux .tar.gz of THPS 2X Recomp: the same folder as the AppImage, unpacked
# (2026-11-24; layout as Panzer Dragoon Orta Recompiled's .tar.gz). RecompLauncher installs it when an AppImage cannot
# be unpacked, and players who prefer a plain folder take it. The toolkit's port_release_linux.sh makes the AppImage
# only; this script makes the tarball from that AppImage:
#
#   THPS2X_Recompiled-<version>-Linux-x86_64/
#     app/                 the AppImage's contents (AppRun, usr/, the .desktop file, the icon, recomplauncher.json)
#     thps2x_launcher      the start script: the folder holds game/, saves/, media/, data/ (the toolkit's root rule)
#     README.txt
#     recomplauncher.json  at the top, next to the program (RecompLauncher's rule)
#
#   bash tools/release_linux_tar.sh <the .AppImage> <version> [out dir (default: next to the AppImage)]
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
IMG=$(realpath "${1:?the .AppImage}")
VERSION=${2:?version}
OUT=$(realpath "${3:-$(dirname "$IMG")}")
NAME=THPS2X_Recompiled-$VERSION-Linux-x86_64
TMP=$(mktemp -d)
trap 'rm -rf -- "$TMP"' EXIT

(cd "$TMP" && "$IMG" --appimage-extract >/dev/null)
mkdir -p "$TMP/$NAME"
mv "$TMP/squashfs-root" "$TMP/$NAME/app"
cp "$REPO/recomplauncher.json" "$TMP/$NAME/recomplauncher.json"

cat > "$TMP/$NAME/thps2x_launcher" <<'EOF'
#!/bin/sh
# Start of THPS 2X Recomp (Linux .tar.gz). The game, saves and settings stay in this folder.
# APPIMAGE = this script: the toolkit takes the folder that holds it as the player's folder (host_paths.c).
SELF="$(readlink -f "$0")"
HERE="$(dirname "$SELF")"
APPIMAGE="$SELF"
APPDIR="$HERE/app"
export APPIMAGE APPDIR
exec "$APPDIR/AppRun" "$@"
EOF
chmod 755 "$TMP/$NAME/thps2x_launcher"

cat > "$TMP/$NAME/README.txt" <<EOF
THPS 2X Recomp $VERSION - Linux (.tar.gz)

Start: run ./thps2x_launcher in this folder (or double-click it).
The first start asks for your disc image (Tony Hawk's Pro Skater 2X, original Xbox, USA) and builds the game into
this folder (the game itself: data/thps2x). That takes a few minutes, once.
Everything (game, saves, settings, logs) stays in this folder - move or delete the folder as a whole.
Updating: replace this folder's program files with the new ones (app/, thps2x_launcher, README.txt,
recomplauncher.json); game/, saves/, media/ and data/ stay.
The same program as the AppImage, unpacked. Needs glibc 2.35 or newer and a Vulkan 1.3 driver.
Licences: app/usr/share/thps2x-recomp/tools/licenses/ (+ tools/python/LICENSES, tools/llvm/LICENSES).
EOF

# sorted, owner-neutral entries: the same input gives the same tarball
tar --sort=name --owner=0 --group=0 --numeric-owner -C "$TMP" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz ($(du -k "$OUT/$NAME.tar.gz" | cut -f1) KB)"
