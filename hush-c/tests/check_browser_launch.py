#!/usr/bin/env python3
"""Capture actual launcher argv with fake browsers, including relay reuse."""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen


SOURCE = Path(__file__).resolve().parents[1]


def check_launch(directory, browser, reopen):
    directory.mkdir()
    executables = directory / "bin"
    executables.mkdir()
    capture = directory / "argv.json"
    stub = executables / browser
    stub.write_text(f"#!{sys.executable}\n"
                    "import json, os, sys\n"
                    "from pathlib import Path\n"
                    "Path(os.environ['HUSH_TEST_CAPTURE']).write_text(json.dumps(sys.argv[1:]))\n")
    stub.chmod(0o755)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    runtime = directory / "runtime"
    runtime.mkdir(mode=0o700)
    hush_home = directory / "home with spaces"
    environment = dict(os.environ, PATH=str(executables), HUSH_HOME=str(hush_home),
                       HUSH_CONFIG_DIR=str(directory / "config"),
                       PASSWORD_STORE_DIR=str(directory / "pass"),
                       XDG_RUNTIME_DIR=str(runtime), HUSH_TEST_CAPTURE=str(capture))
    environment.pop("DISPLAY", None)
    with (directory / "relay.log").open("w") as log:
        process = subprocess.Popen([str(SOURCE / "hush-relay"),
                                    "--no-open" if reopen else "--open", str(port)],
                                   env=environment, stdout=log, stderr=log)
        try:
            for _ in range(100):
                try:
                    with urlopen(f"http://127.0.0.1:{port}/api/status", timeout=1):
                        break
                except OSError:
                    time.sleep(0.03)
            else:
                raise AssertionError("throwaway relay did not start")
            if reopen:
                subprocess.run([str(SOURCE / "hush-relay"), "--open", str(port)],
                               env=environment, stdout=log, stderr=log, check=True, timeout=10)
            for _ in range(100):
                if capture.exists():
                    break
                time.sleep(0.03)
            assert capture.exists(), "launcher did not execute the browser"
            arguments = json.loads(capture.read_text())
            profile = str(hush_home / f"browser-{port}")
            assert f"--user-data-dir={profile}" in arguments, (
                f"{browser}: launcher must isolate Hush from the normal browser profile: {arguments}"
            )
            assert "--class=hush-relay" in arguments
            assert "--ozone-platform=x11" in arguments
            assert f"--app=http://127.0.0.1:{port}/" in arguments
            if browser == "flatpak":
                permission = f"--filesystem={profile}:create"
                assert permission in arguments
                assert arguments.index(permission) < arguments.index("com.brave.Browser")
            print(f"browser launch: {browser}, reopen={reopen}: isolated profile and class OK")
        finally:
            process.terminate()
            process.wait(timeout=10)


def main():
    with tempfile.TemporaryDirectory(prefix="hush-browser-check-") as temporary:
        root = Path(temporary)
        for browser in ["chromium", "flatpak"]:
            for reopen in [False, True]:
                check_launch(root / f"{browser}-{reopen}", browser, reopen)
    print("browser launch check: OK")


if __name__ == "__main__":
    main()
