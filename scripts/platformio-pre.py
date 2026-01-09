Import("env")
import os

print("[pio] pre: configuring build environment")

# Add include path for LVGL configuration file
# This ensures LVGL library can find lv_conf.h during compilation
project_dir = env.get("PROJECT_DIR")
ui_dir = os.path.join(project_dir, "src", "ui")

# Add to both CPPFLAGS (for C/C++ compilation) and CCFLAGS (for all compilation)
env.Append(CPPFLAGS=["-I" + ui_dir])
env.Append(CCFLAGS=["-I" + ui_dir])

# Ensure LV_CONF_INCLUDE_SIMPLE is defined for library compilation
env.Append(CPPDEFINES=["LV_CONF_INCLUDE_SIMPLE"])

print(f"[pio] pre: Added LVGL config include path: {ui_dir}")

# Example hook: add a build flag to verify hook execution
env.Append(CPPDEFINES=["PIO_PRE_HOOK=1"])
