Import("env")

import re


print("[pio] post: build finished")

# Example hook: emit final build environment details
print("[pio] post: build type:", env.get("BUILD_TYPE", "unknown"))


def verify_release_esp32_tinyusb_dfu_disabled(target, source, env):
    dfu_trimmed_envs = {
        "tlora_pager_sx1262",
        "tlora_pager_lr1121",
        "tdeck",
        "tdeck_pro_a7682e",
        "tdeck_pro_pcm512a",
    }
    if env.get("PIOENV") not in dfu_trimmed_envs:
        return

    map_path = env.subst("$BUILD_DIR/${PROGNAME}.map")
    with open(map_path, "r", encoding="utf-8", errors="replace") as map_file:
        map_contents = map_file.read()

    forbidden_members = (
        "dfu_device.c.obj",
        "dfu_rt_device.c.obj",
        ".bss._dfu_ctx",
    )
    managed_pager = env.get("PIOENV") in {
        "tlora_pager_sx1262", "tlora_pager_lr1121"
    }
    managed_tdeck = env.get("PIOENV") == "tdeck"
    if not (managed_pager or managed_tdeck):
        forbidden_members += ("USBCDC.cpp.o",)
    if any(member in map_contents for member in forbidden_members):
        raise RuntimeError(
            "Release image contains a forbidden USB component; "
            "DFU must be absent and CDC is allowed only on managed targets"
        )
    if managed_pager or managed_tdeck:
        required = ("USBCDC.cpp.o", "hostlink_service.cpp.o", "usb_cdc_transport.cpp.o")
        if any(member not in map_contents for member in required):
            raise RuntimeError("Pager/T-Deck release must link USB CDC and HostLink management")
        print("[pio] post: verified Pager/T-Deck CDC management present and DFU absent")
    else:
        print("[pio] post: verified application USB CDC and DFU fully removed from release")


def verify_debug_esp32_tinyusb_dfu_enabled(target, source, env):
    dfu_debug_envs = {
        "tlora_pager_sx1262_debug",
        "tlora_pager_lr1121_debug",
        "tdeck_debug",
        "tdeck_pro_a7682e_debug",
    }
    if env.get("PIOENV") not in dfu_debug_envs:
        return

    map_path = env.subst("$BUILD_DIR/${PROGNAME}.map")
    with open(map_path, "r", encoding="utf-8", errors="replace") as map_file:
        map_contents = map_file.read()

    required_markers = (
        "dfu_device.c.obj",
        "dfu_rt_device.c.obj",
        ".bss._dfu_ctx",
        "trail_mate_debug_tinyusb_dfu_link_anchor",
        "tinyusb_dfu_debug.cpp.o",
        "load_dfu_ota_descriptor",
        "USBCDC.cpp.o",
    )
    missing_markers = [marker for marker in required_markers if marker not in map_contents]
    if missing_markers:
        raise RuntimeError(
            "Arduino TinyUSB CDC + full DFU is incomplete in debug build; "
            f"missing: {', '.join(missing_markers)}"
        )

    descriptor_provider = re.compile(
        r"load_dfu_ota_descriptor\(unsigned char\*, unsigned char\*\)\s+"
        r".*tinyusb_dfu_debug\.cpp\.o"
    )
    if not descriptor_provider.search(map_contents):
        raise RuntimeError(
            "Debug build linked Arduino's empty DFU descriptor instead of the "
            "Trail Mate OTA DFU implementation"
        )
    print("[pio] post: verified Arduino TinyUSB CDC + full DFU in debug build")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", verify_release_esp32_tinyusb_dfu_disabled)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", verify_debug_esp32_tinyusb_dfu_enabled)
