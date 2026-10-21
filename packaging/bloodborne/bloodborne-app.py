#!/usr/bin/env python3
"""Bloodborne: a small GTK4 / libadwaita front end. Pick a profile, press Play.
It runs the `bloodborne` launcher (same folder as the installed command) and shows its output."""
import json, os, shutil, signal, subprocess, sys
from pathlib import Path
import gi
gi.require_version("Gtk", "4.0"); gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk

CONF = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "bloodborne-pc"
APP_JSON, GAME_CONF = CONF / "app.json", CONF / "config"
DEFAULT_GAME = Path.home() / "Games/Bloodborne/CUSA03173"
PROFILES = [("smooth", "Smooth - 50 FPS, tuned for 4 GB GPUs"), ("quality", "Quality - sharper, 45 FPS"),
            ("battery", "Battery saver - 30 FPS, coolest")]
LAUNCHER = shutil.which("bloodborne") or str(Path.home() / ".local/bin/bloodborne")


def load():
    try:
        return json.loads(APP_JSON.read_text())
    except Exception:
        return {}


def game_dir():
    try:
        for line in GAME_CONF.read_text().splitlines():
            if line.startswith("GAME_DIR="):
                return Path(line.split("=", 1)[1].strip().strip('"'))
    except OSError:
        pass
    return DEFAULT_GAME


class Window(Adw.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title="Bloodborne", default_width=520, default_height=640)
        self.proc, self.watch = None, None
        self.state = load()
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(Adw.HeaderBar())
        page = Adw.PreferencesPage()
        box.set_vexpand(True); page.set_vexpand(True)
        box.append(page)
        self.set_content(box)

        group = Adw.PreferencesGroup(title="Play")
        page.add(group)
        self.profile = Adw.ComboRow(title="Profile", model=Gtk.StringList.new([p[1] for p in PROFILES]))
        names = [p[0] for p in PROFILES]
        self.profile.set_selected(names.index(self.state.get("profile", "smooth")) if self.state.get("profile") in names else 0)
        self.profile.connect("notify::selected", lambda *_: self.save())
        group.add(self.profile)
        self.fresh = Adw.SwitchRow(title="New character", subtitle="Uses a separate empty save; your real save is untouched")
        self.log = Adw.SwitchRow(title="Save a performance log", subtitle="Frame times and statistics, in the logs folder")
        self.fresh.set_active(False); self.log.set_active(bool(self.state.get("log")))
        self.log.connect("notify::active", lambda *_: self.save())
        group.add(self.fresh); group.add(self.log)

        gg = Adw.PreferencesGroup(title="Game files")
        page.add(gg)
        self.folder = Adw.ActionRow(title="Game folder")
        choose = Gtk.Button(label="Choose...", valign=Gtk.Align.CENTER)
        choose.connect("clicked", self.pick_folder)
        self.folder.add_suffix(choose)
        gg.add(self.folder)
        self.status = Adw.ActionRow(title="Status")
        gg.add(self.status)

        self.play = Gtk.Button(label="Play", halign=Gtk.Align.CENTER, margin_top=6, margin_bottom=6)
        self.play.add_css_class("suggested-action"); self.play.add_css_class("pill")
        self.play.connect("clicked", self.toggle)
        pg = Adw.PreferencesGroup(); pg.add(self.play); page.add(pg)

        lg = Adw.PreferencesGroup(title="Output")
        self.buf = Gtk.TextBuffer()
        tv = Gtk.TextView(buffer=self.buf, editable=False, monospace=True, cursor_visible=False)
        self.scroll = Gtk.ScrolledWindow(min_content_height=160, child=tv)
        self.scroll.add_css_class("card")
        lg.add(self.scroll); page.add(lg)
        self.refresh()

    def save(self):
        CONF.mkdir(parents=True, exist_ok=True)
        APP_JSON.write_text(json.dumps({"profile": PROFILES[self.profile.get_selected()][0], "log": self.log.get_active()}))

    def refresh(self):
        g = game_dir()
        self.folder.set_subtitle(str(g))
        ok = (g / "eboot.bin").is_file()
        built = Path(LAUNCHER).exists()
        self.status.set_subtitle("Ready" if ok and built else ("Game files not found in this folder" if not ok else "Launcher is not installed"))
        self.play.set_sensitive(ok and built)

    def pick_folder(self, *_):
        dialog = Gtk.FileDialog(title="Choose the game folder (contains eboot.bin)")
        def done(d, res):
            try:
                f = d.select_folder_finish(res)
            except GLib.Error:
                return
            CONF.mkdir(parents=True, exist_ok=True)
            GAME_CONF.write_text(f'GAME_DIR="{f.get_path()}"\n')
            self.refresh()
        dialog.select_folder(self, None, done)

    def toggle(self, *_):
        if self.proc: self.stop(); return
        args = [LAUNCHER, PROFILES[self.profile.get_selected()][0]]
        if self.fresh.get_active(): args.append("--fresh")
        if self.log.get_active(): args.append("--log")
        self.buf.set_text("")
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.watch = GLib.io_add_watch(self.proc.stdout.fileno(), GLib.IO_IN | GLib.IO_HUP, self.on_out)
        self.play.set_label("Stop"); self.play.remove_css_class("suggested-action"); self.play.add_css_class("destructive-action")
        self.status.set_subtitle("Starting... the first launch after an update can take a few minutes")
        GLib.timeout_add_seconds(1, self.poll)

    def on_out(self, fd, cond):
        try:
            data = self.proc.stdout.read()
        except Exception:
            data = None
        if data:
            self.buf.insert(self.buf.get_end_iter(), data.decode(errors="replace"))
            adj = self.scroll.get_vadjustment(); adj.set_value(adj.get_upper())
        return not (cond & GLib.IO_HUP)

    def poll(self):
        if self.proc and self.proc.poll() is None:
            return True
        code = self.proc.returncode if self.proc else 0
        self.proc = None
        self.play.set_label("Play"); self.play.remove_css_class("destructive-action"); self.play.add_css_class("suggested-action")
        self.refresh()
        if code not in (0, None, -signal.SIGTERM):
            self.status.set_subtitle(f"The game stopped (exit code {code}). See the output below.")
        return False

    def stop(self):
        try:
            os.killpg(self.proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass


class App(Adw.Application):
    def __init__(self):
        super().__init__(application_id="io.github.bbport.Bloodborne")
    def do_activate(self):
        (self.props.active_window or Window(self)).present()


if __name__ == "__main__":
    sys.exit(App().run(sys.argv))
