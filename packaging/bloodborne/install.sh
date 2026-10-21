#!/usr/bin/env bash
# Installs the Bloodborne launcher for this user: a `bloodborne` command, an application-menu entry
# with an icon and quick actions. Nothing is copied: it runs from this checkout. Remove with --uninstall.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$here/../.." && pwd)
bin=$HOME/.local/bin
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
icons=${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps
if [[ ${1:-} == --uninstall ]]; then
    rm -f "$bin/bloodborne" "$bin/bloodborne-app" "$apps/bloodborne-pc.desktop" "$icons/bloodborne-pc.svg"
    echo "Removed. Settings in ~/.config/bloodborne-pc and your saves were left alone."; exit 0
fi
mkdir -p "$bin" "$apps" "$icons"
sed "s|@ROOT@|$root|" "$here/bloodborne" > "$bin/bloodborne"; chmod +x "$bin/bloodborne"
cp "$here/bloodborne.svg" "$icons/bloodborne-pc.svg"
cp "$here/bloodborne-app.py" "$bin/bloodborne-app"; chmod +x "$bin/bloodborne-app"
cat > "$apps/bloodborne-pc.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Bloodborne
Comment=Native PC port (smooth profile: 50 FPS, tuned for 4 GB GPUs)
Exec=python3 $bin/bloodborne-app
Icon=bloodborne-pc
Categories=Game;
Terminal=false
StartupNotify=true
StartupWMClass=io.github.bbport.Bloodborne
Actions=Play;Quality;Battery;Fresh;Log;

[Desktop Action Play]
Name=Play now (no window)
Exec=$bin/bloodborne smooth

[Desktop Action Quality]
Name=Quality (sharper, 45 FPS)
Exec=$bin/bloodborne quality

[Desktop Action Battery]
Name=Battery saver (30 FPS)
Exec=$bin/bloodborne battery

[Desktop Action Fresh]
Name=New character (separate save)
Exec=$bin/bloodborne smooth --fresh

[Desktop Action Log]
Name=Play and save a performance log
Exec=$bin/bloodborne smooth --log
DESK
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" 2>/dev/null || true
case ":$PATH:" in *":$bin:"*) ;; *) echo "Note: add $bin to your PATH to use the 'bloodborne' command in a terminal." ;; esac
"$bin/bloodborne" --check && echo "Installed. Launch 'Bloodborne' from the application menu, or run: bloodborne"
