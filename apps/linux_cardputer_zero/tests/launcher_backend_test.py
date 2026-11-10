"""Execute the actual APPLaunch wrapper against observable backend stubs."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import sys

launcher = Path(sys.argv[1]).resolve()
source = launcher.read_text()
probe_start = source.index("find_st7789v_fbdev()")
probe_end = source.index("\n}\n", probe_start) + 3
probe = source[probe_start:probe_end] + '\nfind_st7789v_fbdev "$1"\n'
with tempfile.TemporaryDirectory(prefix="trailmate-launcher-") as temporary:
    root = Path(temporary)
    framebuffer_list = root / "proc-fb"
    for content, expected in (("0 panel-mipi-dbid\n", "/dev/fb0"), ("1 fb_st7789v\n", "/dev/fb1")):
        framebuffer_list.write_text(content)
        detected = subprocess.check_output(["sh", "-c", probe, "framebuffer-probe", str(framebuffer_list)], text=True).strip()
        assert detected == expected, detected
    framebuffer_list.write_text("0 unrelated-display\n")
    assert subprocess.run(["sh", "-c", probe, "framebuffer-probe", str(framebuffer_list)], capture_output=True).returncode != 0
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
    assert result == f"fbdev|{framebuffer}|{keyboard}", result
    environment.pop("TRAIL_MATE_FRAMEBUFFER")
    environment["APPLAUNCH_LINUX_FBDEV_DEVICE"] = str(framebuffer)
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result == f"fbdev|{framebuffer}|{keyboard}", result
    environment["TRAIL_MATE_DISPLAY_BACKEND"] = "wayland"
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result.startswith("wayland|"), result
    environment["TRAIL_MATE_DISPLAY_BACKEND"] = "auto"
    environment.pop("APPLAUNCH_LINUX_FBDEV_DEVICE")
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result.startswith("wayland|"), result
    environment["APPLAUNCH_LINUX_FBDEV_DEVICE"] = str(framebuffer)
    environment["TRAIL_MATE_DISPLAY_BACKEND"] = "fbdev"
    result = subprocess.check_output(["sh", str(wrapper)], env=environment, text=True)
    assert result == f"fbdev|{framebuffer}|{keyboard}", result
print("APPLaunch auto/explicit backend selection PASS")
