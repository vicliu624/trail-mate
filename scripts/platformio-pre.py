Import("env")
import os
import subprocess
import sys
import json
import re

print("[pio] pre: configuring build environment")

project_dir = env.get("PROJECT_DIR")
pio_platform = (env.get("PIOPLATFORM") or "").lower()
is_esp32_env = "espressif32" in pio_platform
is_nrf52_env = "nordicnrf52" in pio_platform
pio_env = env.get("PIOENV")
is_nrf52_node_env = pio_env in ("gat562_mesh_evb_pro", "t-echo-lite")


def read_text_best_effort(path):
    encodings = ("utf-16", "utf-8-sig", "utf-8")
    last_error = None
    for encoding in encodings:
        try:
            with open(path, "r", encoding=encoding) as fp:
                return fp.read()
        except UnicodeError as exc:
            last_error = exc
    if last_error:
        raise last_error
    with open(path, "r", encoding="utf-8") as fp:
        return fp.read()


def read_source_text_for_guard(path):
    with open(path, "rb") as fp:
        data = fp.read()

    for encoding in ("utf-8-sig", "utf-8", "utf-16"):
        try:
            return data.decode(encoding)
        except UnicodeError:
            continue

    return data.decode("utf-8", errors="ignore")


def extract_version_from_changelog():
    changelog_path = os.path.join(project_dir, "CHANGELOG.md")
    if not os.path.exists(changelog_path):
        return None

    text = read_text_best_effort(changelog_path)
    match = re.search(r"^## \[([0-9][^\]]*)\]", text, re.MULTILINE)
    return match.group(1).strip() if match else None


def extract_version_from_ci_tag():
    github_ref = os.environ.get("GITHUB_REF", "")
    github_ref_name = os.environ.get("GITHUB_REF_NAME", "")

    tag_name = ""
    if github_ref.startswith("refs/tags/"):
        tag_name = github_ref.rsplit("/", 1)[-1]
    elif os.environ.get("GITHUB_REF_TYPE") == "tag" and github_ref_name:
        tag_name = github_ref_name

    if not tag_name:
        return None
    return tag_name[1:] if tag_name.startswith("v") else tag_name


def resolve_project_version():
    version = extract_version_from_ci_tag()
    if version:
        return version

    version = extract_version_from_changelog()
    if version:
        return version

    return "unknown"


def update_library_build_metadata(library_json_path, desired_updates, description):
    if not os.path.exists(library_json_path):
        print(f"[pio] pre: {description} metadata not found yet: {library_json_path}")
        return

    with open(library_json_path, "r", encoding="utf-8") as fp:
        library_json = json.load(fp)

    build_section = dict(library_json.get("build", {}))
    changed = False

    for key, value in desired_updates.items():
        if build_section.get(key) != value:
            build_section[key] = value
            changed = True

    if not changed:
        print(f"[pio] pre: {description} metadata already trimmed")
        return

    library_json["build"] = build_section

    with open(library_json_path, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(library_json, fp, indent=4)
        fp.write("\n")

    print(f"[pio] pre: Trimmed {description} build metadata")


def create_library_build_metadata(
    library_json_path,
    manifest_defaults,
    desired_updates,
    description,
):
    if os.path.exists(library_json_path):
        update_library_build_metadata(
            library_json_path,
            desired_updates,
            description,
        )
        return

    library_dir = os.path.dirname(library_json_path)
    if not os.path.isdir(library_dir):
        print(f"[pio] pre: {description} metadata not found yet: {library_json_path}")
        return

    library_json = dict(manifest_defaults)
    library_json["build"] = dict(desired_updates)
    with open(library_json_path, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(library_json, fp, indent=4)
        fp.write("\n")

    print(f"[pio] pre: Created trimmed {description} build metadata")


def iter_active_project_source_files():
    source_roots = ("apps", "boards", "modules", "platform")
    source_extensions = (".c", ".cc", ".cpp", ".h", ".hpp", ".ino")
    skipped_dirs = {
        ".git",
        ".pio",
        ".tmp",
        ".distinction",
        "docs",
        "Best Practices",
        "build",
        "dist",
    }

    for source_root in source_roots:
        root_path = os.path.join(project_dir, source_root)
        if not os.path.isdir(root_path):
            continue

        for current_dir, dir_names, file_names in os.walk(root_path):
            dir_names[:] = [
                name for name in dir_names
                if name not in skipped_dirs and not name.startswith(".")
            ]
            for file_name in file_names:
                if file_name.endswith(source_extensions):
                    yield os.path.join(current_dir, file_name)


def guard_no_arduino_sd_audio_paths():
    if not is_esp32_env:
        return

    forbidden_patterns = (
        (
            "ESP8266Audio Arduino SD source",
            re.compile(r"\bAudioFileSourceSD\b"),
        ),
        (
            "Arduino SD include",
            re.compile(r"#\s*include\s*[<\"]SD\.h[>\"]"),
        ),
        (
            "Arduino FS include",
            re.compile(r"#\s*include\s*[<\"]FS\.h[>\"]"),
        ),
        (
            "Arduino SD singleton",
            re.compile(r"(?<![A-Za-z0-9_])SD\."),
        ),
        (
            "Arduino fs::FS binding",
            re.compile(r"(?<![A-Za-z0-9_:])(?:::)?fs::FS\b"),
        ),
    )

    violations = []
    for path in iter_active_project_source_files():
        text = read_source_text_for_guard(path)
        for description, pattern in forbidden_patterns:
            for match in pattern.finditer(text):
                line_no = text.count("\n", 0, match.start()) + 1
                rel_path = os.path.relpath(path, project_dir)
                violations.append((rel_path, line_no, description))

    if not violations:
        return

    print("[pio] pre: forbidden Arduino SD/FS path detected in active source:")
    for rel_path, line_no, description in violations[:20]:
        print(f"[pio] pre:   {rel_path}:{line_no}: {description}")
    if len(violations) > 20:
        print(f"[pio] pre:   ... {len(violations) - 20} more")
    raise RuntimeError(
        "Arduino SD/FS access is retired on ESP32; use SdRuntimeFile/SdRuntimeDir "
        "or the LVGL/SdRuntime storage adapters."
    )


def configure_radiolib_for_gat562():
    if not is_nrf52_node_env:
        return

    project_dir = env.get("PROJECT_DIR")
    radiolib_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "RadioLib")
    library_json_path = os.path.join(radiolib_dir, "library.json")
    desired_src_filter = [
        "-<*>",
        "+<Hal.cpp>",
        "+<Module.cpp>",
        "+<hal/Arduino/ArduinoHal.cpp>",
        "+<protocols/PhysicalLayer/*.cpp>",
        "+<protocols/Print/*.cpp>",
        "+<utils/*.cpp>",
        "+<modules/SX126x/SX1262.cpp>",
        "+<modules/SX126x/SX126x.cpp>",
        "+<modules/SX126x/SX126x_LR_FHSS.cpp>",
        "+<modules/SX126x/SX126x_commands.cpp>",
        "+<modules/SX126x/SX126x_config.cpp>",
    ]
    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"RadioLib for {pio_env}",
    )


def configure_crypto_for_gat562():
    if not is_nrf52_node_env:
        return

    crypto_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "Crypto")
    library_json_path = os.path.join(crypto_dir, "library.json")
    desired_src_filter = [
        "-<*>",
        "+<AES128.cpp>",
        "+<AES256.cpp>",
        "+<AESCommon.cpp>",
        "+<BigNumberUtil.cpp>",
        "+<BlockCipher.cpp>",
        "+<CTR.cpp>",
        "+<ChaCha.cpp>",
        "+<Cipher.cpp>",
        "+<Crypto.cpp>",
        "+<Curve25519.cpp>",
        "+<Hash.cpp>",
        "+<RNG.cpp>",
        "+<SHA256.cpp>",
    ]
    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"Crypto for {pio_env}",
    )


def configure_esp8266audio_for_esp32():
    if not is_esp32_env:
        return

    audio_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "ESP8266Audio")
    library_json_path = os.path.join(audio_dir, "library.json")
    # Pager has its own audio path. T-Deck only uses the I2S output and its
    # logger definition; keep the full-library fallback for other ESP boards.
    if pio_env in ("tlora_pager_sx1262", "tlora_pager_sx1262_debug"):
        desired_src_filter = ["-<*>"]
    elif pio_env in ("tdeck", "tdeck_debug"):
        desired_src_filter = [
            "-<*>",
            "+<AudioLogger.cpp>",
            "+<AudioOutputI2S.cpp>",
        ]
    else:
        desired_src_filter = [
            "+<*>",
            "-<AudioFileSourceSD.cpp>",
        ]
    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"ESP8266Audio for {pio_env}",
    )


def configure_explicit_audio_driver_instances():
    # audio-driver 0.3.1 instantiates every codec in AudioDriver.h, including
    # codecs with nontrivial constructors. Offer an opt-out in the env-local
    # dependency; the board then owns only its actual codec. Other environments
    # and translation units retain the upstream default.
    if not is_esp32_env or pio_env != "wio_tracker_l2":
        return
    library_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "audio-driver")
    header = os.path.join(library_dir, "src", "AudioDriver.h")
    if not os.path.isfile(header):
        raise RuntimeError("audio-driver dependency is required for WIO codec configuration")
    with open(os.path.join(library_dir, "library.properties"), encoding="utf-8") as fp:
        properties = fp.read()
    if "version=0.3.1" not in properties.splitlines():
        raise RuntimeError("Review the explicit-instance patch for this audio-driver version")
    with open(header, encoding="utf-8") as fp:
        source = fp.read()
    guard = "#ifndef TRAIL_MATE_AUDIO_DRIVER_EXPLICIT_INSTANCES"
    if guard in source:
        return
    start_marker = "// -- Drivers\n"
    end_marker = "}  // namespace audio_driver"
    if source.count(start_marker) != 1 or source.count(end_marker) != 1:
        raise RuntimeError("Unexpected AudioDriver.h layout; refusing to patch codec instances")
    start = source.index(start_marker) + len(start_marker)
    end = source.index(end_marker, start)
    instances = source[start:end]
    if "static AudioDriverES8311Class AudioDriverES8311;" not in instances:
        raise RuntimeError("Expected ES8311 instance declaration missing")
    patched = source[:start] + guard + "\n" + instances + "#endif\n\n" + source[end:]
    with open(header, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(patched)
    print("[pio] pre: Enabled explicit audio-driver codec instances for WIO")


def configure_radiolib_for_sx1262_esp32():
    if not is_esp32_env or pio_env not in (
        "tlora_pager_sx1262",
        "tlora_pager_sx1262_debug",
        "tdeck",
        "tdeck_debug",
    ):
        return

    radiolib_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "RadioLib")
    library_json_path = os.path.join(radiolib_dir, "library.json")
    # These targets only instantiate SX1262. Avoid compiling every other
    # transceiver and protocol implementation that RadioLib ships.
    desired_src_filter = [
        "-<*>",
        "+<Hal.cpp>",
        "+<Module.cpp>",
        "+<hal/Arduino/ArduinoHal.cpp>",
        "+<protocols/PhysicalLayer/*.cpp>",
        "+<protocols/Print/*.cpp>",
        "+<utils/*.cpp>",
        "+<modules/SX126x/SX1262.cpp>",
        "+<modules/SX126x/SX126x.cpp>",
        "+<modules/SX126x/SX126x_LR_FHSS.cpp>",
        "+<modules/SX126x/SX126x_commands.cpp>",
        "+<modules/SX126x/SX126x_config.cpp>",
    ]
    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"RadioLib for {pio_env}",
    )


def configure_lvgl_for_esp32_ui():
    if not is_esp32_env or pio_env not in (
        "tlora_pager_sx1262",
        "tlora_pager_sx1262_debug",
        "tdeck",
        "tdeck_debug",
    ):
        return

    lvgl_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "lvgl")
    library_json_path = os.path.join(lvgl_dir, "library.json")
    # Keep this list aligned with platform/esp/arduino_common/include/lv_conf.h.
    # Disabled desktop drivers, render backends, and codecs otherwise still
    # create hundreds of empty or unreachable compilation units.
    desired_src_filter = [
        "-<*>",
        "+<lv_init.c>",
        "+<core/>",
        "+<display/>",
        "+<draw/*.c>",
        "+<draw/convert/lv_draw_buf_convert.c>",
        "+<draw/sw/>",
        "+<font/lv_binfont_loader.c>",
        "+<font/lv_font.c>",
        "+<font/lv_font_fmt_txt.c>",
        "+<font/lv_font_montserrat_*.c>",
        "+<indev/>",
        "+<layouts/>",
        "+<misc/>",
        "+<osal/lv_os.c>",
        "+<osal/lv_os_none.c>",
        "+<stdlib/lv_mem.c>",
        "+<stdlib/builtin/lv_sprintf_builtin.c>",
        "+<stdlib/builtin/lv_string_builtin.c>",
        "+<themes/>",
        "+<tick/>",
        "+<widgets/>",
        "+<libs/bin_decoder/>",
        "+<libs/bmp/>",
        "+<libs/fsdrv/lv_fs_memfs.c>",
        "+<libs/gif/>",
        "+<libs/lodepng/>",
        "+<libs/qrcode/>",
        "+<libs/tjpgd/>",
        "+<others/observer/>",
        "+<others/snapshot/>",
    ]
    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"LVGL for {pio_env}",
    )


def configure_crypto_for_sx1262_esp32():
    if not is_esp32_env or pio_env not in (
        "tlora_pager_sx1262",
        "tlora_pager_sx1262_debug",
        "tdeck",
        "tdeck_debug",
    ):
        return

    crypto_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "Crypto")
    library_json_path = os.path.join(crypto_dir, "library.json")
    # This is the linked object closure for AES-CTR, ChaCha-Poly1305,
    # Curve25519, HKDF, RNG, and SHA-256 used by the supported protocols.
    # VMP private MQTT uses HKDF even on the SX1262 Pager, whose VMP carrier
    # is MQTT-only and therefore never takes the LR1121 RF handshake path.
    desired_src_filter = [
        "-<*>",
        "+<AESEsp32.cpp>",
        "+<AuthenticatedCipher.cpp>",
        "+<BigNumberUtil.cpp>",
        "+<BlockCipher.cpp>",
        "+<ChaCha.cpp>",
        "+<ChaChaPoly.cpp>",
        "+<Cipher.cpp>",
        "+<Crypto.cpp>",
        "+<Curve25519.cpp>",
        "+<Hash.cpp>",
        "+<HKDF.cpp>",
        "+<Poly1305.cpp>",
        "+<RNG.cpp>",
        "+<SHA256.cpp>",
    ]
    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"Crypto for {pio_env}",
    )


def configure_sensorlib_for_sx1262_esp32():
    if not is_esp32_env or pio_env not in (
        "tlora_pager_sx1262",
        "tlora_pager_sx1262_debug",
        "tdeck",
        "tdeck_debug",
    ):
        return

    sensor_dir = os.path.join(project_dir, ".pio", "libdeps", pio_env, "SensorLib")
    library_json_path = os.path.join(sensor_dir, "library.json")
    # Both boards use BHI260AP. T-Deck additionally needs the GT911 base
    # implementation; its concrete driver is header-only in SensorLib 0.3.3.
    desired_src_filter = [
        "-<*>",
        "+<SensorBHI260AP.cpp>",
        "+<bosch/BoschParseStatic.cpp>",
        "+<bosch/bhy2.c>",
        "+<bosch/bhy2_hif.c>",
        "+<bosch/common/common.cpp>",
        "+<platform/SensorCommStatic.cpp>",
    ]
    if pio_env in ("tdeck", "tdeck_debug"):
        desired_src_filter.append("+<TouchDrvInterface.cpp>")

    update_library_build_metadata(
        library_json_path,
        {"srcFilter": desired_src_filter},
        f"SensorLib for {pio_env}",
    )


def configure_sdfat_for_sx1262_esp32():
    if not is_esp32_env or pio_env not in (
        "tlora_pager_sx1262",
        "tlora_pager_sx1262_debug",
        "tdeck",
        "tdeck_debug",
    ):
        return

    sdfat_dir = os.path.join(project_dir, "third_party", "sdfat")
    library_json_path = os.path.join(sdfat_dir, "library.json")
    # Preserve FAT32/exFAT and the generic Arduino SPI path while excluding
    # formatters, streams, debug helpers, and other architectures' drivers.
    desired_src_filter = [
        "-<*>",
        "+<ExFatLib/ExFatFile.cpp>",
        "+<ExFatLib/ExFatFilePrint.cpp>",
        "+<ExFatLib/ExFatFileWrite.cpp>",
        "+<ExFatLib/ExFatName.cpp>",
        "+<ExFatLib/ExFatPartition.cpp>",
        "+<ExFatLib/ExFatVolume.cpp>",
        "+<FatLib/FatFile.cpp>",
        "+<FatLib/FatFileLFN.cpp>",
        "+<FatLib/FatFilePrint.cpp>",
        "+<FatLib/FatName.cpp>",
        "+<FatLib/FatPartition.cpp>",
        "+<FatLib/FatVolume.cpp>",
        "+<FsLib/FsFile.cpp>",
        "+<FsLib/FsNew.cpp>",
        "+<FsLib/FsVolume.cpp>",
        "+<SdCard/SdCardInfo.cpp>",
        "+<SdCard/SdSpiCard/SdSpiCard.cpp>",
        "+<SdCard/SdSpiCard/SpiDriver/SdSpiChipSelect.cpp>",
        "+<common/FmtNumber.cpp>",
        "+<common/FsCache.cpp>",
        "+<common/FsDateTime.cpp>",
        "+<common/FsName.cpp>",
        "+<common/FsUtf.cpp>",
        "+<common/upcase.cpp>",
    ]
    create_library_build_metadata(
        library_json_path,
        {
            "name": "SdFat",
            "version": "2.3.1",
            "frameworks": ["arduino"],
            "platforms": ["*"],
        },
        {"srcFilter": desired_src_filter},
        f"SdFat for {pio_env}",
    )


def configure_nrf52_framework_libraries():
    if not is_nrf52_node_env or not is_nrf52_env:
        return

    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinoadafruitnrf52")
    if not framework_dir:
        return

    tinyusb_cppdefines = [
        ("CFG_TUD_CDC", 1),
        ("CFG_TUD_MSC", 0),
        ("CFG_TUD_HID", 0),
        ("CFG_TUD_MIDI", 0),
        ("CFG_TUD_VENDOR", 0),
        ("CFG_TUD_VIDEO", 0),
        ("CFG_TUD_VIDEO_STREAMING", 0),
    ]
    env.AppendUnique(CPPDEFINES=tinyusb_cppdefines)
    print(f"[pio] pre: Restricted TinyUSB device classes for {pio_env}")

    bluefruit_json_path = os.path.join(framework_dir, "libraries", "Bluefruit52Lib", "library.json")
    bluefruit_src_filter = [
        "-<*>",
        "+<BLEAdvertising.cpp>",
        "+<BLECentral.cpp>",
        "+<BLECharacteristic.cpp>",
        "+<BLEClientCharacteristic.cpp>",
        "+<BLEClientService.cpp>",
        "+<BLEConnection.cpp>",
        "+<BLEDiscovery.cpp>",
        "+<BLEGatt.cpp>",
        "+<BLEPeriph.cpp>",
        "+<BLEScanner.cpp>",
        "+<BLESecurity.cpp>",
        "+<BLEService.cpp>",
        "+<BLEUuid.cpp>",
        "+<bluefruit.cpp>",
        "+<utility/*.cpp>",
    ]
    update_library_build_metadata(
        bluefruit_json_path,
        {"srcFilter": bluefruit_src_filter},
        f"Bluefruit52Lib for {pio_env}",
    )

    tinyusb_json_path = os.path.join(framework_dir, "libraries", "Adafruit_TinyUSB_Arduino", "library.json")
    tinyusb_src_filter = [
        "-<*>",
        "+<tusb.c>",
        "+<arduino/Adafruit_TinyUSB_API.cpp>",
        "+<arduino/Adafruit_USBD_CDC.cpp>",
        "+<arduino/Adafruit_USBD_Device.cpp>",
        "+<arduino/Adafruit_USBD_Interface.cpp>",
        "+<arduino/ports/nrf/Adafruit_TinyUSB_nrf.cpp>",
        "+<class/cdc/cdc_device.c>",
        "+<common/tusb_fifo.c>",
        "+<device/usbd.c>",
        "+<device/usbd_control.c>",
        "+<portable/nordic/nrf5x/dcd_nrf5x.c>",
    ]
    update_library_build_metadata(
        tinyusb_json_path,
        {"srcFilter": tinyusb_src_filter, "libArchive": False},
        f"Adafruit_TinyUSB_Arduino for {pio_env}",
    )


def filter_product_specific_ui_mono_screens():
    current_directory = os.path.abspath(project_dir)
    while not os.path.isdir(os.path.join(current_directory, "modules", "ui_mono")):
        parent_directory = os.path.dirname(current_directory)
        if parent_directory == current_directory:
            raise RuntimeError("Unable to locate the Trail Mate repository root")
        current_directory = parent_directory

    screen_source_root = os.path.join(
        current_directory,
        "modules",
        "ui_mono",
        "src",
        "screens",
    )
    product_screen_families = {
        "gat562_mesh_evb_pro": "screen_128x64",
        "t-echo-lite": "screen_192x176",
    }
    permitted_screen_family = product_screen_families.get(pio_env)
    if pio_env.startswith("tdeck_pro_"):
        permitted_screen_family = "screen_240x320"

    product_screen_prefixes = [
        os.path.normcase(os.path.join(screen_source_root, screen_family)) + os.sep
        for screen_family in (
            "screen_128x64",
            "screen_192x176",
            "screen_240x320",
        )
    ]
    permitted_screen_prefix = (
        os.path.normcase(os.path.join(screen_source_root, permitted_screen_family)) + os.sep
        if permitted_screen_family
        else None
    )

    def exclude_foreign_product_screen_source(node):
        source_path = os.path.normcase(os.path.abspath(node.srcnode().get_path()))
        if source_path.startswith(tuple(product_screen_prefixes)):
            if permitted_screen_prefix and source_path.startswith(permitted_screen_prefix):
                return node
            return None
        return node

    env.AddBuildMiddleware(exclude_foreign_product_screen_source)
    selected_family = permitted_screen_family or "none"
    print(f"[pio] pre: Selected ui_mono product screen family: {selected_family}")


def disable_arduino_tinyusb_dfu_for_release_esp32():
    # Arduino-ESP32 ships both TinyUSB DFU device classes precompiled for every
    # ESP32-S3 memory profile. Release Pager and T-Deck builds do not expose an
    # application USB maintenance interface: flashing remains available through
    # the ESP32-S3 ROM downloader after the physical BOOT + RESET sequence.
    #
    # Remove both Arduino DFU drivers from a build-private archive. In addition
    # to the full DFU driver's permanent 4 KiB transfer buffer, this keeps DFU
    # Runtime out of a release image entirely. Debug environments deliberately
    # retain the original archive so they can expose CDC + full DFU together.
    #
    # Keep this list deliberately ESP32-only: nRF52 targets have their own
    # TinyUSB source/configuration and retain their DFU support unchanged.
    dfu_disabled_envs = {
        "tlora_pager_sx1262",
        "tlora_pager_lr1121",
        "tdeck",
        "tdeck_pro_a7682e",
        "tdeck_pro_pcm512a",
        "wio_tracker_l2",
    }
    if not is_esp32_env or pio_env not in dfu_disabled_envs:
        return

    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinoespressif32")
    if not framework_dir:
        raise RuntimeError("Arduino-ESP32 framework package is required for TinyUSB DFU trimming")

    board_config = env.BoardConfig()
    mcu = board_config.get("build.mcu")
    source_archive = os.path.join(
        framework_dir,
        "tools",
        "sdk",
        mcu,
        "lib",
        "libarduino_tinyusb.a",
    )
    if not os.path.isfile(source_archive):
        raise RuntimeError(f"Arduino TinyUSB archive not found: {source_archive}")

    # The directory name is part of the transformation version. It prevents an
    # archive created by an earlier full-DFU-only optimization from being reused
    # after DFU Runtime was also removed.
    archive_dir = os.path.join(env.subst("$BUILD_DIR"), "trail_mate_tinyusb_no_dfu_v2")
    os.makedirs(archive_dir, exist_ok=True)
    trimmed_archive = os.path.join(archive_dir, "libarduino_tinyusb.a")
    toolchain_dir = platform.get_package_dir(f"toolchain-xtensa-{mcu}")
    if not toolchain_dir:
        raise RuntimeError(f"ESP32 toolchain package not found for {mcu}")
    ar_name_candidates = (
        f"xtensa-{mcu}-elf-ar",
        f"xtensa-{mcu}-elf-gcc-ar",
        env.subst("$AR"),
    )
    ar_candidates = []
    for ar_name in ar_name_candidates:
        candidate = os.path.join(toolchain_dir, "bin", ar_name)
        ar_candidates.append(candidate)
        if os.name == "nt":
            ar_candidates.append(candidate + ".exe")
    ar_path = next((candidate for candidate in ar_candidates if os.path.isfile(candidate)), None)
    if not ar_path:
        raise RuntimeError(
            "ESP32 archive tool not found; checked: " + ", ".join(ar_candidates)
        )
    forbidden_members = ("dfu_device.c.obj", "dfu_rt_device.c.obj")

    def archive_still_contains_dfu_members():
        if not os.path.isfile(trimmed_archive):
            return True
        try:
            archive_members = subprocess.check_output(
                [ar_path, "t", trimmed_archive],
                text=True,
            )
        except (OSError, subprocess.CalledProcessError):
            return True
        return any(member in archive_members.splitlines() for member in forbidden_members)

    archive_needs_refresh = (
        not os.path.isfile(trimmed_archive)
        or os.path.getmtime(trimmed_archive) < os.path.getmtime(source_archive)
        or archive_still_contains_dfu_members()
    )
    if archive_needs_refresh:
        with open(source_archive, "rb") as source_fp, open(trimmed_archive, "wb") as target_fp:
            target_fp.write(source_fp.read())
        subprocess.check_call(
            [
                ar_path,
                "d",
                trimmed_archive,
                "dfu_device.c.obj",
                "dfu_rt_device.c.obj",
            ],
        )
        archive_members = subprocess.check_output(
            [ar_path, "t", trimmed_archive],
            text=True,
        )
        remaining_members = [
            member for member in forbidden_members if member in archive_members.splitlines()
        ]
        if remaining_members:
            raise RuntimeError(
                "Failed to remove Arduino TinyUSB DFU archive members: "
                f"{', '.join(remaining_members)}"
            )

    # PlatformIO selects this SDK archive through -larduino_tinyusb. Search
    # this build-private directory first so the original package remains
    # untouched and every CI/local build gets the same trimmed copy.
    env.Prepend(LIBPATH=[archive_dir])

    env.AppendUnique(
        CPPDEFINES=[
            ("TRAIL_MATE_DISABLE_ARDUINO_TINYUSB_DFU", 1),
        ]
    )
    print(f"[pio] pre: Disabled Arduino TinyUSB DFU classes for release {pio_env}")


def inject_project_version_define():
    version = resolve_project_version()
    escaped_version = version.replace("\\", "\\\\").replace('"', '\\"')
    env.AppendUnique(CPPDEFINES=[("TRAIL_MATE_FIRMWARE_VERSION", '\\"' + escaped_version + '\\"')])
    print(f"[pio] pre: Injected firmware version: {version}")


guard_no_arduino_sd_audio_paths()
configure_radiolib_for_gat562()
configure_crypto_for_gat562()
configure_esp8266audio_for_esp32()
configure_explicit_audio_driver_instances()
configure_radiolib_for_sx1262_esp32()
configure_lvgl_for_esp32_ui()
configure_crypto_for_sx1262_esp32()
configure_sensorlib_for_sx1262_esp32()
configure_sdfat_for_sx1262_esp32()
disable_arduino_tinyusb_dfu_for_release_esp32()
configure_nrf52_framework_libraries()
filter_product_specific_ui_mono_screens()
inject_project_version_define()

# Only ESP Arduino builds need the shared LVGL config under platform/esp.
if is_esp32_env:
    ui_dir = os.path.join(project_dir, "platform", "esp", "arduino_common", "include")
    env.Append(CPPFLAGS=["-I" + ui_dir])
    env.Append(CCFLAGS=["-I" + ui_dir])
    env.Append(CPPDEFINES=["LV_CONF_INCLUDE_SIMPLE"])
    print(f"[pio] pre: Added LVGL config include path: {ui_dir}")

# Nordic builds should use their own shared runtime headers instead of falling
# back to ESP include roots via unrelated global injection.
if is_nrf52_env:
    nrf_ui_dir = os.path.join(project_dir, "platform", "nrf52", "arduino_common", "include")
    env.Append(CPPFLAGS=["-I" + nrf_ui_dir])
    env.Append(CCFLAGS=["-I" + nrf_ui_dir])
    print(f"[pio] pre: Added nRF52 runtime include path: {nrf_ui_dir}")

# On nordicnrf52/Windows builds, `${platformio.packages_dir}` inside `build_flags`
# can be rewritten into an invalid builder-local include path. Inject the
# nRF52 framework paths here using the resolved absolute package location so
# framework libraries like `Wire` can include `<Adafruit_TinyUSB.h>`.
if is_nrf52_env:
    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinoadafruitnrf52")
    if framework_dir:
        framework_include_candidates = [
            os.path.join(framework_dir, "libraries", "Adafruit_TinyUSB_Arduino", "src"),
            os.path.join(framework_dir, "libraries", "Adafruit_TinyUSB_Arduino", "src", "arduino"),
            os.path.join(framework_dir, "libraries", "SPI"),
            os.path.join(framework_dir, "libraries", "Wire"),
            os.path.join(framework_dir, "libraries", "Bluefruit52Lib", "src"),
            os.path.join(framework_dir, "libraries", "Bluefruit52Lib", "src", "services"),
            os.path.join(framework_dir, "libraries", "Adafruit_nRFCrypto", "src"),
            os.path.join(framework_dir, "libraries", "Adafruit_LittleFS", "src"),
            os.path.join(framework_dir, "libraries", "InternalFileSytem", "src"),
        ]
        existing_cpppath = set(env.get("CPPPATH", []))
        include_paths = [path for path in framework_include_candidates if os.path.isdir(path)]
        missing_paths = [path for path in include_paths if path not in existing_cpppath]
        if missing_paths:
            env.Append(CPPPATH=missing_paths)
            print(f"[pio] pre: Added nRF52 framework include paths: {', '.join(missing_paths)}")

# Generate protobuf files if .proto files exist
proto_dir = os.path.join(project_dir, "lib", "meshtastic_protobufs")
generate_script = os.path.join(project_dir, "scripts", "generate_protobuf.py")

if os.path.exists(generate_script) and os.path.exists(proto_dir):
    print("[pio] pre: Generating protobuf files...")
    try:
        result = subprocess.run([sys.executable, generate_script], 
                              cwd=project_dir, capture_output=True, text=True)
        if result.returncode == 0:
            print("[pio] pre: Protobuf generation successful")
        else:
            print(f"[pio] pre: Protobuf generation warning: {result.stderr}")
    except Exception as e:
        print(f"[pio] pre: Protobuf generation error: {e}")

# Add generated protobuf include path
generated_dir = os.path.join(project_dir, "src", "chat", "infra", "meshtastic", "generated")
if os.path.exists(generated_dir):
    env.Append(CPPFLAGS=["-I" + generated_dir])
    env.Append(CCFLAGS=["-I" + generated_dir])
    print(f"[pio] pre: Added protobuf generated include path: {generated_dir}")

# Example hook: add a build flag to verify hook execution
env.Append(CPPDEFINES=["PIO_PRE_HOOK=1"])
