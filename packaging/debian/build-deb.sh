#!/usr/bin/env bash
# Builds dist/bbport_<version>_amd64.deb from the current build (run build.sh first).
# Debian/Ubuntu only; no Nix. The package holds the engine, the online module, the scripts and the GTK
# launcher; the game files stay yours. Saves, settings and generated files go to ~/.local/share/bbport.
# usage: packaging/debian/build-deb.sh [version]
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
root=$PWD
version=${1:-0.6.0+bbport1}
[[ -f out/bb-probe && -f out/gpu/libbbgpu.so ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
stage=$(mktemp -d); trap 'rm -rf "$stage"' EXIT
lib=$stage/usr/lib/bbport
mkdir -p "$lib/bin/gpu" "$lib/tools" "$stage/usr/bin" "$stage/usr/share/applications" \
         "$stage/usr/share/icons/hicolor/scalable/apps" "$stage/DEBIAN"
install -m755 run.sh "$lib/"
cp -r scripts patches launcher "$lib/"
[[ -d fsr4_shaders ]] && cp -r fsr4_shaders "$lib/"
cp -r tools/fsr4cap "$lib/tools/"
find "$lib" -name __pycache__ -prune -exec rm -rf {} +
install -m755 out/bb-probe out/bb-gpu-capabilities "$lib/bin/"
for so in libbbgpu libbbnet libbbnet20; do
    [[ -f out/gpu/$so.so ]] && install -m755 out/gpu/$so.so "$lib/bin/gpu/"
done
strip --strip-unneeded "$lib"/bin/bb-probe "$lib"/bin/bb-gpu-capabilities "$lib"/bin/gpu/*.so 2>/dev/null || true
cp LICENSE "$lib/" 2>/dev/null || true

cat > "$stage/usr/bin/bbport" <<'WRAP'
#!/bin/sh
# Bloodborne (native port): opens the launcher. `bbport --play` starts the game with the saved settings.
export BB_PREBUILT=1
export BB_DATA_DIR="${BB_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/bbport}"
mkdir -p "$BB_DATA_DIR"
cd /usr/lib/bbport || exit 1
exec python3 /usr/lib/bbport/launcher/bbport_launcher.py "$@"
WRAP
chmod 755 "$stage/usr/bin/bbport"
install -m644 "$root/packaging/debian/bbport.svg" "$stage/usr/share/icons/hicolor/scalable/apps/bbport.svg"
cat > "$stage/usr/share/applications/bbport.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=Bloodborne
GenericName=Bloodborne (native port)
Comment=Launcher for the native Bloodborne port: settings, online co-op, play
Exec=bbport
Icon=bbport
Categories=Game;
Terminal=false
StartupNotify=true
StartupWMClass=io.github.bbport.Launcher
Actions=Play;

[Desktop Action Play]
Name=Play now (saved settings)
Exec=bbport --play
DESK


# Linux-style extras: man page, AppStream metadata, copyright and changelog.
doc=$stage/usr/share/doc/bbport; man=$stage/usr/share/man/man1; meta=$stage/usr/share/metainfo
mkdir -p "$doc" "$man" "$meta"
cat > "$man/bbport.1" <<'MAN'
.TH BBPORT 1 "October 2026" "bbport" "Games"
.SH NAME
bbport \- native Bloodborne port launcher
.SH SYNOPSIS
.B bbport
[\fB\-\-play\fR]
.SH DESCRIPTION
Opens the launcher: game folder, upscaler and graphics settings, online play and seamless co\-op.
With \fB\-\-play\fR the game starts at once with the saved settings and no window.
The package contains no game files; choose your own Bloodborne 1.09 dump in the launcher.
.SH FILES
.TP
.I ~/.local/share/bbport
Saves, generated files, bbport.ini and logs.
.TP
.I ~/.config/bbport\-launcher/settings.json
Launcher settings, including the online account (readable only by you).
.SH ENVIRONMENT
.TP
.B BB_DATA_DIR
Use another data directory.
.TP
.B BB_FPS_LIMIT
Frame rate cap.
.SH SEE ALSO
https://github.com/deadinside28/bloodborne_pc
MAN
gzip -9n "$man/bbport.1"
cat > "$meta/io.github.bbport.Launcher.metainfo.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>io.github.bbport.Launcher</id>
  <name>Bloodborne (native port)</name>
  <summary>Run your own Bloodborne dump natively on Linux</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-2.0-or-later</project_license>
  <description>
    <p>A launcher and engine that run your own legally dumped Bloodborne 1.09 natively on Vulkan,
    with temporal upscaling and optional online play and seamless co-op. No game files are included.</p>
  </description>
  <launchable type="desktop-id">bbport.desktop</launchable>
  <url type="homepage">https://github.com/deadinside28/bloodborne_pc</url>
  <categories><category>Game</category></categories>
  <content_rating type="oars-1.1"><content_attribute id="violence-realistic">intense</content_attribute></content_rating>
  <provides><binary>bbport</binary></provides>
</component>
XML
cat > "$doc/copyright" <<'CPR'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: bbport
Source: https://github.com/deadinside28/bloodborne_pc

Files: *
Copyright: bbport contributors
License: GPL-2.0+

Files: usr/lib/bbport/bin/gpu/libbbnet20.so
Copyright: bbport contributors, shadPS4 contributors, droogie
License: GPL-3.0+

License: GPL-2.0+
 On Debian systems the full text is in /usr/share/common-licenses/GPL-2.
License: GPL-3.0+
 On Debian systems the full text is in /usr/share/common-licenses/GPL-3.
CPR
{ echo "bbport ($version) unstable; urgency=medium"; echo; echo "  * Debian package of upstream 0.6 with the protobuf CMake fallback and game-folder autodetection."; echo; echo " -- Ogden Marrow <jpalenchar.3@gmail.com>  $(date -R)"; } | gzip -9n > "$doc/changelog.Debian.gz"
find "$stage/usr" -type d -exec chmod 755 {} +

# Dependencies: the packages that own the shared libraries the binaries load, plus the launcher's.
libs=$( { for f in "$lib"/bin/bb-probe "$lib"/bin/bb-gpu-capabilities "$lib"/bin/gpu/*.so; do ldd "$f"; done; } \
        | awk '/=>/ && $3 ~ /^\// {print $3}' | sort -u )
deps=$( for l in $libs; do dpkg -S "$(readlink -f "$l")" 2>/dev/null | cut -d: -f1; done | sort -u | grep -vE '^(libc6|libgcc-s1|libstdc\+\+6|libm6)$' | tr '\n' ',' )
deps="libc6, libstdc++6, libgcc-s1, ${deps}python3, python3-gi, gir1.2-gtk-4.0, gir1.2-adw-1, ca-certificates, mesa-vulkan-drivers | nvidia-vulkan-icd, vulkan-tools"
size=$(du -sk "$stage/usr" | cut -f1)
cat > "$stage/DEBIAN/control" <<CTL
Package: bbport
Version: $version
Section: games
Priority: optional
Architecture: amd64
Maintainer: Ogden Marrow <jpalenchar.3@gmail.com>
Installed-Size: $size
Depends: ${deps%,}
Homepage: https://github.com/deadinside28/bloodborne_pc
Description: Native Bloodborne port (launcher, engine and online co-op module)
 Runs your own Bloodborne 1.09 dump natively on Linux with a Vulkan renderer, temporal upscaling
 and optional online play / seamless co-op. Contains no game files: choose the game folder in the
 launcher. Settings and saves live in ~/.local/share/bbport.
CTL
mkdir -p "$root/dist"
out=$root/dist/bbport_${version}_amd64.deb
fakeroot dpkg-deb -Zxz --build "$stage" "$out" >/dev/null
ls -lh "$out"; dpkg-deb -I "$out" | grep -E "Depends|Installed" | cut -c1-300
