#!/usr/bin/env python3
"""Exercise native window setup against an isolated X server, never the desktop."""

import ctypes as ct
import os
from pathlib import Path
import selectors
import shlex
import shutil
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().parents[1]
STRICT_FLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow"]


def build_library(directory, x11):
    library = directory / ("win-x11.so" if x11 else "win-stub.so")
    command = shlex.split(os.environ.get("CC", "cc")) + STRICT_FLAGS
    command += ["-shared", "-fPIC", "-I" + str(SOURCE / "include")]
    if x11:
        command += ["-DHUSH_HAVE_X11=1"]
    command += [str(SOURCE / "src/hush_win.c"), "-o", str(library)]
    if x11:
        command += ["-lX11"]
    subprocess.run(command, check=True)
    return ct.CDLL(str(library))


def bind(library, name, arguments, result=ct.c_int):
    function = getattr(library, name)
    function.argtypes = arguments
    function.restype = result
    return function


class Display:
    def __init__(self, name):
        self.library = ct.CDLL("libX11.so.6")
        pointer, word = ct.c_void_p, ct.c_ulong
        self.open = bind(self.library, "XOpenDisplay", [ct.c_char_p], pointer)
        self.close = bind(self.library, "XCloseDisplay", [pointer])
        self.root_window = bind(self.library, "XDefaultRootWindow", [pointer], word)
        self.create = bind(self.library, "XCreateSimpleWindow", [
            pointer, word, ct.c_int, ct.c_int, ct.c_uint, ct.c_uint,
            ct.c_uint, word, word,
        ], word)
        self.intern = bind(self.library, "XInternAtom", [pointer, ct.c_char_p, ct.c_int], word)
        self.change = bind(self.library, "XChangeProperty", [
            pointer, word, word, word, ct.c_int, ct.c_int, pointer, ct.c_int,
        ])
        self.get = bind(self.library, "XGetWindowProperty", [
            pointer, word, word, ct.c_long, ct.c_long, ct.c_int, word,
            ct.POINTER(word), ct.POINTER(ct.c_int), ct.POINTER(word),
            ct.POINTER(word), ct.POINTER(pointer),
        ])
        self.delete = bind(self.library, "XDeleteProperty", [pointer, word, word])
        self.sync = bind(self.library, "XSync", [pointer, ct.c_int])
        self.free = bind(self.library, "XFree", [pointer])
        self.handle = self.open(name.encode())
        assert self.handle, "could not open isolated Xvfb"
        self.root = self.root_window(self.handle)

    def atom(self, name):
        return self.intern(self.handle, name.encode(), 0)

    def set_words(self, window, name, kind, values):
        payload = (ct.c_ulong * len(values))(*values)
        self.change(self.handle, window, self.atom(name), self.atom(kind), 32, 0,
                    payload, len(values))
        self.sync(self.handle, 0)

    def words(self, window, name):
        kind, count, remaining = ct.c_ulong(), ct.c_ulong(), ct.c_ulong()
        format_bits, payload = ct.c_int(), ct.c_void_p()
        result = self.get(self.handle, window, self.atom(name), 0, 256, 0, 0,
                          ct.byref(kind), ct.byref(format_bits), ct.byref(count),
                          ct.byref(remaining), ct.byref(payload))
        assert result == 0, "XGetWindowProperty failed"
        try:
            assert remaining.value == 0, "test property was truncated"
            if not kind.value:
                return None
            assert format_bits.value == 32, "unexpected property format"
            values = ct.cast(payload, ct.POINTER(ct.c_ulong))
            return [values[index] for index in range(count.value)]
        finally:
            if payload:
                self.free(payload)

    def window(self, instance, name):
        window = self.create(self.handle, self.root, 0, 0, 320, 240, 0, 0, 0)
        assert window
        payload = instance.encode() + b"\0" + name.encode() + b"\0"
        self.change(self.handle, window, self.atom("WM_CLASS"), self.atom("STRING"),
                    8, 0, ct.c_char_p(payload), len(payload))
        self.sync(self.handle, 0)
        return window


def check_properties(native, display):
    other = display.window("other-app", "OtherApp")
    window = display.window("hush-relay", "Hush")
    display.set_words(display.root, "_NET_CLIENT_LIST", "WINDOW", [other, window])
    names = ["WM_DELETE_WINDOW", "_NET_WM_SYNC_REQUEST", "_NET_WM_PING", "HUSH_TEST_CUSTOM"]
    original = [display.atom(name) for name in names]
    expected = [original[0], original[2], original[3]]
    display.set_words(other, "WM_PROTOCOLS", "ATOM", original)

    for desktop in [None, "", "GNOME", "KDE", "COSMICITY", "notCOSMIC"]:
        if desktop is None:
            os.environ.pop("XDG_CURRENT_DESKTOP", None)
        else:
            os.environ["XDG_CURRENT_DESKTOP"] = desktop
        display.set_words(window, "WM_PROTOCOLS", "ATOM", original)
        assert native.hush_win_undecorate() == 0
        assert display.words(window, "WM_PROTOCOLS") == original, desktop
    print("window check: other desktops retain resize synchronization", flush=True)

    for desktop in ["COSMIC", "pop:COSMIC", "COSMIC:pop", "pop:COSMIC:extra"]:
        os.environ["XDG_CURRENT_DESKTOP"] = desktop
        display.set_words(window, "WM_PROTOCOLS", "ATOM", original)
        assert native.hush_win_undecorate() == 0
        assert display.words(window, "WM_PROTOCOLS") == expected, (
            f"{desktop}: resize synchronization must be removed; close/ping/custom must remain"
        )
        assert display.words(window, "_MOTIF_WM_HINTS") == [2, 0, 6, 0, 0]
        assert native.hush_win_undecorate() == 0
        assert display.words(window, "WM_PROTOCOLS") == expected
    assert display.words(other, "WM_PROTOCOLS") == original
    assert display.words(other, "_MOTIF_WM_HINTS") is None
    print("window check: COSMIC setup preserves other protocols/windows and is repeatable", flush=True)

    for protocols in [[], [original[0]], [original[1]], [original[1], original[1]]]:
        display.set_words(window, "WM_PROTOCOLS", "ATOM", protocols)
        assert native.hush_win_undecorate() == 0
        assert display.words(window, "WM_PROTOCOLS") == [p for p in protocols if p != original[1]]
    display.delete(display.handle, window, display.atom("WM_PROTOCOLS"))
    display.sync(display.handle, 0)
    assert native.hush_win_undecorate() == 0
    assert display.words(window, "WM_PROTOCOLS") is None
    oversized = original * 17
    display.set_words(window, "WM_PROTOCOLS", "ATOM", oversized)
    assert native.hush_win_undecorate() == -3
    assert display.words(window, "WM_PROTOCOLS") == oversized
    display.set_words(display.root, "_NET_CLIENT_LIST", "WINDOW", [other])
    assert native.hush_win_undecorate() == -4
    print("window check: empty/missing/oversized protocols and absent Hush window handled", flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix="hush-win-check-") as temporary:
        directory = Path(temporary)
        stub = build_library(directory, False)
        for name in ["hush_win_undecorate", "hush_win_minimize", "hush_win_maximize"]:
            assert getattr(stub, name)() == -6
        print("window check: no-X11 stubs return HUSH_ERR_IO", flush=True)
        if not shutil.which("Xvfb"):
            print("SKIP native window properties: Xvfb not installed")
            return
        native = build_library(directory, True)
        read_fd, write_fd = os.pipe()
        with (directory / "xvfb.log").open("w") as log:
            server = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
                                       "800x600x24", "-nolisten", "tcp"],
                                      pass_fds=(write_fd,), stdout=log, stderr=log)
            os.close(write_fd)
            display = None
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(read_fd, selectors.EVENT_READ)
                    assert selector.select(timeout=10), "Xvfb startup timed out"
                number = os.read(read_fd, 32).decode().strip()
                assert number.isdigit(), "Xvfb failed to allocate a display"
                os.environ["DISPLAY"] = ":" + number
                display = Display(os.environ["DISPLAY"])
                check_properties(native, display)
            finally:
                os.close(read_fd)
                if display:
                    display.close(display.handle)
                server.terminate()
                server.wait(timeout=10)
    print("window check: OK")


if __name__ == "__main__":
    main()
