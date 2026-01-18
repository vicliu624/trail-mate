Import("env")
import os
import subprocess
import sys

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
