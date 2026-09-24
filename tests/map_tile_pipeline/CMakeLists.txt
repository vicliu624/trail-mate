cmake_minimum_required(VERSION 3.20)
project(map_tile_pipeline LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(REPO "${CMAKE_CURRENT_LIST_DIR}/../..")
enable_testing()
add_executable(map_tile_poi_retry test_poi_retry.cpp
    ${REPO}/modules/ui_map_runtime/src/map_poi/poi_tile_source.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp)
target_include_directories(map_tile_poi_retry PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include)
add_test(NAME map_tile_poi_retry COMMAND map_tile_poi_retry)
add_executable(map_tile_metrics test_pipeline_metrics.cpp)
target_include_directories(map_tile_metrics PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/core_sys/include)
add_test(NAME map_tile_metrics COMMAND map_tile_metrics)
add_executable(map_tile_file_source
    ${REPO}/modules/ui_map_runtime/tests/test_filesystem_map_tile_source.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp)
target_include_directories(map_tile_file_source PRIVATE ${REPO}/modules/ui_map_runtime/include)
add_test(NAME map_tile_file_source COMMAND map_tile_file_source)
add_executable(map_tile_completion test_completion_queue.cpp)
target_include_directories(map_tile_completion PRIVATE ${REPO}/modules/ui_map_runtime/include)
add_test(NAME map_tile_completion COMMAND map_tile_completion)
add_executable(map_tile_requests test_request_queue.cpp)
target_include_directories(map_tile_requests PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/core_sys/include)
add_test(NAME map_tile_requests COMMAND map_tile_requests)
add_executable(map_tile_async
    ${REPO}/modules/ui_map_runtime/tests/test_map_tile_async_runtime.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_async_runtime.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp)
target_include_directories(map_tile_async PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/core_sys/include)
add_test(NAME map_tile_async COMMAND map_tile_async)
add_executable(map_tile_esp_completion test_esp_completion.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_async_runtime.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp)
target_include_directories(map_tile_esp_completion PRIVATE stubs
    ${REPO}/platform/esp/arduino_common/include
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/core_sys/include)
find_package(Threads REQUIRED)
target_link_libraries(map_tile_esp_completion PRIVATE Threads::Threads)
add_test(NAME map_tile_esp_completion COMMAND map_tile_esp_completion)
foreach(mode on off)
    add_executable(map_tile_diagnostics_${mode} test_diagnostics.cpp
        ${REPO}/platform/esp/arduino_common/src/platform_ui_map_diagnostics.cpp)
    target_include_directories(map_tile_diagnostics_${mode} PRIVATE stubs
        ${REPO}/modules/core_sys/include ${REPO}/modules/ui_shared/include)
    if(mode STREQUAL "on")
        target_compile_definitions(map_tile_diagnostics_${mode} PRIVATE TRAIL_MATE_MAP_DIAGNOSTICS=1)
    endif()
    add_test(NAME map_tile_diagnostics_${mode} COMMAND map_tile_diagnostics_${mode})
endforeach()
foreach(target map_tile_completion map_tile_requests map_tile_async map_tile_esp_completion map_tile_metrics map_tile_file_source map_tile_poi_retry map_tile_diagnostics_on map_tile_diagnostics_off)
    set_tests_properties(${target} PROPERTIES TIMEOUT 30)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra)
    endif()
endforeach()
