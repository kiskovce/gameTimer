#!/usr/bin/env python3
"""Opakujuci sa casovac: po uplynuti periody vyskoci nad ostatne okna, zapipa a blika."""
import json
import math
import os
import shutil
import struct
import subprocess
import tempfile
import time
import wave

# Pod Waylandom GNOME blokuje "focus stealing"; cez XWayland funguje present()/keep_above.
if os.environ.get("DISPLAY"):
    os.environ.setdefault("GDK_BACKEND", "x11")

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GLib, Gtk  # noqa: E402

CONFIG_PATH = os.path.join(
    os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config")),
    "gametimer",
    "settings.json",
)
DEFAULTS = {"hours": 0, "minutes": 25, "seconds": 0, "blink_seconds": 10, "sound": True}

CSS = b"""
.alert-on  { background: #d32f2f; }
.alert-on label  { color: white; }
.alert-off { background: #fff176; }
.alert-off label { color: black; }
.countdown { font-size: 48px; font-weight: bold; }
"""


def load_settings():
    try:
        with open(CONFIG_PATH, encoding="utf-8") as f:
            return {**DEFAULTS, **json.load(f)}
    except (OSError, ValueError):
        return dict(DEFAULTS)


def save_settings(settings):
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=2)


def make_beep_file():
    path = os.path.join(tempfile.gettempdir(), f"gametimer-beep-{os.getuid()}.wav")
    rate, dur, freq = 44100, 0.35, 880
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        n = int(rate * dur)
        for i in range(n):
            fade = min(1.0, i / 500, (n - i) / 500)
            frames += struct.pack("<h", int(20000 * fade * math.sin(2 * math.pi * freq * i / rate)))
        w.writeframes(bytes(frames))
    return path


def play_sound(path):
    for cmd in (["paplay"], ["pw-play"], ["aplay", "-q"]):
        if shutil.which(cmd[0]):
            subprocess.Popen(cmd + [path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return
    Gdk.beep()


class TimerWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="Game Timer")
        self.set_border_width(0)
        self.set_default_size(360, 320)
        self.settings = load_settings()
        self.beep_path = make_beep_file()
        self.deadline = None
        self.alert_end = None
        self.blink_state = False
        self.last_beep = 0.0

        provider = Gtk.CssProvider()
        provider.load_from_data(CSS)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        self.root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.root.set_border_width(16)
        self.add(self.root)

        self.countdown = Gtk.Label(label="00:00:00")
        self.countdown.get_style_context().add_class("countdown")
        self.root.pack_start(self.countdown, False, False, 8)

        grid = Gtk.Grid(column_spacing=8, row_spacing=8, halign=Gtk.Align.CENTER)
        self.h = self._spin(0, 99, self.settings["hours"])
        self.m = self._spin(0, 59, self.settings["minutes"])
        self.s = self._spin(0, 59, self.settings["seconds"])
        self.blink = self._spin(1, 3600, self.settings["blink_seconds"])
        grid.attach(Gtk.Label(label="Perióda (h : m : s)"), 0, 0, 1, 1)
        row = Gtk.Box(spacing=4)
        for w in (self.h, self.m, self.s):
            row.pack_start(w, False, False, 0)
        grid.attach(row, 1, 0, 1, 1)
        grid.attach(Gtk.Label(label="Blikanie (s)"), 0, 1, 1, 1)
        grid.attach(self.blink, 1, 1, 1, 1)
        self.sound = Gtk.CheckButton(label="Zvukový alarm")
        self.sound.set_active(self.settings["sound"])
        grid.attach(self.sound, 0, 2, 2, 1)
        self.root.pack_start(grid, False, False, 0)

        self.start_btn = Gtk.Button(label="Štart")
        self.start_btn.connect("clicked", self.on_start_stop)
        self.root.pack_start(self.start_btn, False, False, 0)

        self.dismiss_btn = Gtk.Button(label="Potvrdiť (zastaviť blikanie)")
        self.dismiss_btn.connect("clicked", lambda *_: self.stop_alert())
        self.root.pack_start(self.dismiss_btn, False, False, 0)

        self.connect("destroy", self.on_destroy)
        GLib.timeout_add(200, self.tick)
        GLib.timeout_add(400, self.blink_tick)
        self.show_all()
        self.dismiss_btn.hide()

    @staticmethod
    def _spin(lo, hi, value):
        sp = Gtk.SpinButton.new_with_range(lo, hi, 1)
        sp.set_value(value)
        sp.set_numeric(True)
        return sp

    def period(self):
        return int(self.h.get_value()) * 3600 + int(self.m.get_value()) * 60 + int(self.s.get_value())

    def collect_settings(self):
        self.settings = {
            "hours": int(self.h.get_value()),
            "minutes": int(self.m.get_value()),
            "seconds": int(self.s.get_value()),
            "blink_seconds": int(self.blink.get_value()),
            "sound": self.sound.get_active(),
        }
        save_settings(self.settings)

    def on_start_stop(self, _btn):
        if self.deadline is None:
            if self.period() <= 0:
                return
            self.collect_settings()
            self.deadline = time.monotonic() + self.period()
            self.start_btn.set_label("Stop")
        else:
            self.deadline = None
            self.stop_alert()
            self.start_btn.set_label("Štart")
            self.countdown.set_text("00:00:00")

    def tick(self):
        if self.deadline is not None:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                self.fire()
                remaining = self.deadline - time.monotonic()
            r = max(0, math.ceil(remaining))
            self.countdown.set_text(f"{r // 3600:02d}:{r % 3600 // 60:02d}:{r % 60:02d}")
        if self.alert_end is not None and time.monotonic() >= self.alert_end:
            self.stop_alert()
        elif self.alert_end is not None and self.settings["sound"]:
            if time.monotonic() - self.last_beep >= 1.0:
                self.last_beep = time.monotonic()
                play_sound(self.beep_path)
        return True

    def fire(self):
        self.deadline = time.monotonic() + max(1, self.period())
        self.alert_end = time.monotonic() + self.settings["blink_seconds"]
        self.last_beep = 0.0
        self.dismiss_btn.show()
        self.deiconify()
        self.set_keep_above(True)
        self.present()

    def blink_tick(self):
        ctx = self.root.get_style_context()
        ctx.remove_class("alert-on")
        ctx.remove_class("alert-off")
        if self.alert_end is not None:
            self.blink_state = not self.blink_state
            ctx.add_class("alert-on" if self.blink_state else "alert-off")
        return True

    def stop_alert(self):
        self.alert_end = None
        self.set_keep_above(False)
        self.dismiss_btn.hide()

    def on_destroy(self, *_):
        self.collect_settings()
        Gtk.main_quit()


if __name__ == "__main__":
    TimerWindow()
    Gtk.main()
