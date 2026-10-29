include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${TRAIL_MATE_REPO_ROOT}/CHANGELOG.md"
    "${TRAIL_MATE_REPO_ROOT}/scripts/project_version.py"
    "${TRAIL_MATE_REPO_ROOT}/scripts/read_changelog.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${TRAIL_MATE_REPO_ROOT}/scripts/project_version.py"
    OUTPUT_VARIABLE project_version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${TRAIL_MATE_REPO_ROOT}/scripts/project_version.py" --debian
    OUTPUT_VARIABLE debian_version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(TRAIL_MATE_PROJECT_VERSION "${project_version}" CACHE INTERNAL "Resolved project firmware version" FORCE)
set(TRAIL_MATE_DEBIAN_PROJECT_VERSION "${debian_version}" CACHE INTERNAL "Resolved Debian upstream version" FORCE)
configure_file("${CMAKE_CURRENT_LIST_DIR}/templates/linux_build_version.h.in"
    "${CMAKE_BINARY_DIR}/generated/trailmate_linux_build_version.h" @ONLY)
