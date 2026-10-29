"""Apply the actual patch, then exercise its C callbacks with a fake DSI host.

Run on Linux: python3 test_panel_backlight.py VENDOR_PANEL_SOURCE PATCH_FILE
The test does not load a module or modify the supplied source.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def definition(source, pattern):
    match = re.search(pattern, source)
    if not match:
        raise RuntimeError(f"Missing definition: {pattern}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
typedef uint8_t u8;
struct drm_panel { int unused; };
struct device_node { const char *compatible; };
struct device { struct device_node *of_node; };
struct mipi_dsi_host { struct device *dev; };
struct mipi_dsi_device { struct device dev; struct mipi_dsi_host *host; };
static bool of_device_is_compatible(struct device_node *node, const char *name) { return node && !strcmp(node->compatible, name); }
struct gpio_desc { int unused; };
struct mutex { bool locked; };
struct backlight_device { void *data; int brightness; bool blank; };
static unsigned commands[256], count;
static int fail_command = -1, last_brightness, updates;
static bool short_write, zero_status;
#define RM69A10_ENABLE_BIST 0
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define dev_info(dev, ...) do { (void)(dev); } while (0)
static void mutex_lock(struct mutex *lock) { assert(!lock->locked); lock->locked = true; }
static void mutex_unlock(struct mutex *lock) { assert(lock->locked); lock->locked = false; }
static void msleep(unsigned value) { (void)value; }
static void gpiod_set_value_cansleep(struct gpio_desc *gpio, int value) { (void)gpio; (void)value; }
static void *bl_get_data(struct backlight_device *backlight) { return backlight->data; }
static int backlight_get_brightness(struct backlight_device *backlight) { return backlight->blank ? 0 : backlight->brightness; }
static int backlight_update_status(struct backlight_device *backlight) { (void)backlight; ++updates; return 0; }
static ssize_t mipi_dsi_dcs_write_buffer(struct mipi_dsi_device *dsi, const void *buffer, size_t size)
{
    const u8 *bytes = buffer;
    (void)dsi;
    assert(count < 256 && size > 0);
    commands[count++] = bytes[0];
    if (bytes[0] == fail_command) return -EIO;
    if (bytes[0] == 0x51) {
        assert(size == 2);
        if (short_write) return 1;
        last_brightness = bytes[1];
        if (zero_status) return 0;
    }
    return size;
}
#define mipi_dsi_dcs_write_seq(dsi, seq...) do { const u8 bytes[] = {seq}; \
    int result = mipi_dsi_dcs_write_buffer(dsi, bytes, sizeof(bytes)); if (result < 0) return result; } while (0)
static int command(struct mipi_dsi_device *dsi, u8 value) { return mipi_dsi_dcs_write_buffer(dsi, &value, 1) < 0 ? -EIO : 0; }
static int mipi_dsi_dcs_exit_sleep_mode(struct mipi_dsi_device *dsi) { return command(dsi, 0x11); }
static int mipi_dsi_dcs_set_display_on(struct mipi_dsi_device *dsi) { return command(dsi, 0x29); }
static int mipi_dsi_dcs_set_display_off(struct mipi_dsi_device *dsi) { return command(dsi, 0x28); }
static int mipi_dsi_dcs_enter_sleep_mode(struct mipi_dsi_device *dsi) { return command(dsi, 0x10); }
'''

TEST = r'''
int main(void)
{
    struct mipi_dsi_device dsi = {0};
    struct backlight_device backlight = {.brightness = 254, .blank = true};
    struct amoled_panel panel = {.dsi = &dsi, .backlight = &backlight};
    backlight.data = &panel;
    assert(amoled_panel_enable(&panel.panel) == -EPERM);
    assert(!panel.brightness_lock.locked);
    assert(amoled_panel_prepare(&panel.panel) == 0);
    assert(panel.prepared && !panel.enabled && updates == 0 && last_brightness == 0);
    for (unsigned i = 0; i < count; ++i) assert(commands[i] != 0x11 && commands[i] != 0x29);
    unsigned prior = count;
    assert(amoled_panel_update_brightness(&backlight) == 0 && count == prior);
    assert(amoled_panel_enable(&panel.panel) == 0);
    assert(commands[count - 2] == 0x11 && commands[count - 1] == 0x29);
    backlight.blank = false; /* DRM's backlight-enable follows panel enable. */
    assert(amoled_panel_update_brightness(&backlight) == 0 && last_brightness == 254);
    assert(amoled_panel_get_brightness(&backlight) == 254);
    backlight.blank = true;
    assert(amoled_panel_update_brightness(&backlight) == 0 && last_brightness == 0);
    assert(amoled_panel_disable(&panel.panel) == 0);
    assert(panel.prepared && !panel.enabled);
    assert(commands[count - 2] == 0x28 && commands[count - 1] == 0x10);
    prior = count;
    backlight.brightness = 128;
    assert(amoled_panel_update_brightness(&backlight) == 0 && count == prior);
    assert(amoled_panel_get_brightness(&backlight) == 0);
    assert(amoled_panel_enable(&panel.panel) == 0); /* No intervening prepare. */
    backlight.blank = false;
    assert(amoled_panel_update_brightness(&backlight) == 0 && last_brightness == 128);
    assert(amoled_panel_unprepare(&panel.panel) == 0);
    assert(!panel.prepared && !panel.enabled && amoled_panel_get_brightness(&backlight) == 0);
    fail_command = 0x35;
    assert(amoled_panel_prepare(&panel.panel) == -EIO && !panel.prepared);
    fail_command = -1;
    assert(amoled_panel_prepare(&panel.panel) == 0);
    fail_command = 0x11;
    assert(amoled_panel_enable(&panel.panel) == -EIO && !panel.enabled && !panel.brightness_lock.locked);
    fail_command = 0x29;
    assert(amoled_panel_enable(&panel.panel) == -EIO && !panel.enabled && !panel.brightness_lock.locked);
    fail_command = -1;
    assert(amoled_panel_enable(&panel.panel) == 0);
    backlight.brightness = 42;
    assert(amoled_panel_update_brightness(&backlight) == 0);
    backlight.brightness = 64;
    fail_command = 0x51;
    assert(amoled_panel_update_brightness(&backlight) == -EIO);
    assert(amoled_panel_get_brightness(&backlight) == 42);
    fail_command = -1; short_write = true;
    assert(amoled_panel_update_brightness(&backlight) == -EIO);
    assert(amoled_panel_get_brightness(&backlight) == 42);
    short_write = false;
    zero_status = true;
    assert(amoled_panel_update_brightness(&backlight) == -EIO); /* Generic short transfer. */
    struct device_node node = {.compatible = "brcm,bcm2835-dsi1"};
    struct device host_device = {.of_node = &node};
    struct mipi_dsi_host host = {.dev = &host_device};
    dsi.host = &host;
    assert(amoled_panel_update_brightness(&backlight) == 0); /* VC4 completion status. */
    assert(amoled_panel_get_brightness(&backlight) == 64);
    short_write = true;
    backlight.brightness = 100;
    assert(amoled_panel_update_brightness(&backlight) == -EIO); /* Positive short writes still fail. */
    assert(amoled_panel_get_brightness(&backlight) == 64);
    short_write = false; zero_status = false; fail_command = 0x28;
    assert(amoled_panel_disable(&panel.panel) == -EIO);
    assert(!panel.enabled && !panel.brightness_lock.locked);
    assert(amoled_panel_get_brightness(&backlight) == 0);
    return 0;
}
'''


def main():
    base, patch = (Path(argument).resolve() for argument in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="trailmate-panel-test-") as directory:
        root = Path(directory)
        target = root / "panel_rm69a10.c"
        target.write_bytes(base.read_bytes())
        subprocess.run(["patch", "--batch", "--forward", "-p1", "-i", str(patch)], cwd=root, check=True)
        source = target.read_text()
        functions = ["amoled_panel_update_brightness", "amoled_panel_get_brightness", "amoled_panel_prepare",
                     "amoled_panel_enable", "amoled_panel_disable", "amoled_panel_unprepare"]
        body = definition(source, r"struct amoled_panel\s*\{") + ";\n"
        body += "\n".join(definition(source, rf"static int {name}\s*\(") for name in functions)
        harness = root / "callbacks.c"
        harness.write_text(PREFIX + body + TEST)
        executable = root / "callbacks"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        str(harness), "-o", str(executable)], check=True)
        environment = os.environ.copy()
        environment.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        subprocess.run([str(executable)], env=environment, check=True)
        print("Patched panel callbacks: lifecycle and DSI failure tests passed (ASan/UBSan)")


if __name__ == "__main__":
    main()
