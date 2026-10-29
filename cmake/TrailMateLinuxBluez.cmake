include_guard(GLOBAL)
function(trailmate_add_linux_bluez)
    if(TARGET trailmate_linux_bluez)
        return()
    endif()
    add_library(trailmate_linux_bluez
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/bluez_bus.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/bluez_pairing_agent.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/bluez_phone_gatt.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/bluez_adapters.cpp")
    target_include_directories(trailmate_linux_bluez PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
        "${TRAIL_MATE_REPO_ROOT}/platform/shared/include")
    target_compile_features(trailmate_linux_bluez PUBLIC cxx_std_17)
    # Phone BLE is deferred: compile only the unavailable transport, without GIO.
endfunction()
