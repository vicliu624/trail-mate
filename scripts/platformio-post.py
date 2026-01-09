Import("env")

print("[pio] post: build finished")

# Example hook: emit final build environment details
print("[pio] post: build type:", env.get("BUILD_TYPE", "unknown"))
