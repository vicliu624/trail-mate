# PlatformIO library hook. Preserve existing exclusions for all other targets.
Import("env")
env.AppendUnique(CPPPATH=[env.subst("$PROJECT_DIR") + "/modules/core_hostlink/include"])
source_filter = ["+<*>", "-<ble/>", "-<rnode_kiss/>",
                 "-<platform_ui_hostlink_runtime.cpp>", "-<ui/screens/gps/>"]
if env.subst("$PIOENV") not in (
        "wio_tracker_l2", "tlora_pager_sx1262", "tlora_pager_lr1121", "tdeck"):
    source_filter.append("-<hostlink/>")
else:
    env.AppendUnique(CPPPATH=[
        env.subst("$PROJECT_DIR") + "/modules/core_track_sharing/include",
        env.subst("$PROJECT_DIR") + "/platform/esp/idf_common/include",
        env.subst("$PROJECT_DIR") + "/third_party/yxml",
    ])
env.Replace(SRC_FILTER=source_filter)
