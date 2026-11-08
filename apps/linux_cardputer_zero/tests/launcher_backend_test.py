"""Execute the actual APPLaunch wrapper against observable backend stubs."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import sys

launcher = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="trailmate-launcher-") as temporary:
    root = Path(temporary)
    wrapper = root / launcher.name
    shutil.copyfile(launcher, wrapper)
    for name, backend in (("trailmate-cardputer-zero", "wayland"), ("trailmate-cardputer-zero-fbdev", "fbdev")):
        path = root / name
        path.write_text('#!/bin/sh\nprintf "%s|%s|%s" "' + backend + '" "${TRAIL_MATE_FRAMEBUFFER:-}" "${TRAIL_MATE_INPUT_DEVICE:-}"\n')
        path.chmod(0o755)
    framebuffer = root / "framebuffer"
    framebuffer.touch()
    keyboard = root / "keyboard"
    keyboard.touch()
    environment = os.environ.copy()
    environment.update(HOME=str(root), TRAIL_MATE_SETTINGS_ROOT=str(root / "settings"),
                       TRAIL_MATE_DISPLAY_BACKEND="auto", TRAIL_MATE_FRAMEBUFFER=str(framebuffer),
                       TRAIL_MATE_INPUT_DEVICE=str(keyboard))
    environment.pop("WAYLAND_DISPLAY", None)
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result == f"fbdev|{framebuffer}|{keyboard}", result
    environment["WAYLAND_DISPLAY"] = "wayland-test"
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result.startswith("wayland|"), result
    environment["TRAIL_MATE_DISPLAY_BACKEND"] = "fbdev"
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result == f"fbdev|{framebuffer}|{keyboard}", result
print("APPLaunch auto/explicit backend selection PASS")
