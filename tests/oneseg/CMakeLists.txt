cmake_minimum_required(VERSION 3.20)
project(oneseg_receive LANGUAGES C CXX)
enable_testing()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(FM_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(P4_LORA_RUNTIME "${FM_REPO}/platform/esp/idf_common/src/platform_ui_lora_runtime.cpp")
    set(P4_LORA_EXTRACT "${FM_REPO}/tests/broadcast_fm/extract_lora_lease.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${P4_LORA_RUNTIME}" "${P4_LORA_EXTRACT}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${P4_LORA_EXTRACT}"
        "${P4_LORA_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/p4_lora_lease.inc"
        COMMAND_ERROR_IS_FATAL ANY)
    set(P4_WALKIE_RUNTIME "${FM_REPO}/platform/esp/idf_common/src/walkie_runtime.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${P4_WALKIE_RUNTIME}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${P4_LORA_EXTRACT}"
        "${P4_WALKIE_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/p4_walkie_lease.inc" walkie
        COMMAND_ERROR_IS_FATAL ANY)
    add_executable(p4_lora_lease_test ../broadcast_fm/test_p4_lora_lease.cpp)
    target_include_directories(p4_lora_lease_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
        ../../platform/esp/common/include)
    target_compile_features(p4_lora_lease_test PRIVATE cxx_std_17)
    target_compile_options(p4_lora_lease_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(p4_lora_lease_test PRIVATE Threads::Threads)
    add_test(NAME p4_lora_lease COMMAND p4_lora_lease_test)
    set_tests_properties(p4_lora_lease PROPERTIES TIMEOUT 10)
    set(P4_MODE_EXTRACT "${FM_REPO}/tests/broadcast_fm/extract_p4_fm_mode.py")
    set(P4_MODE_DRIVER "${FM_REPO}/platform/esp/idf_common/src/lr2021_radio.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${P4_MODE_EXTRACT}" "${P4_MODE_DRIVER}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${P4_MODE_EXTRACT}" "${P4_MODE_DRIVER}"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_fm_mode_state.inc" "${CMAKE_CURRENT_BINARY_DIR}/p4_fm_mode_config.inc"
        COMMAND_ERROR_IS_FATAL ANY)
    add_executable(p4_fm_mode_test ../broadcast_fm/test_p4_fm_mode.cpp)
    target_include_directories(p4_fm_mode_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
        ../../platform/esp/idf_common/include ../../platform/shared/include)
    target_compile_features(p4_fm_mode_test PRIVATE cxx_std_17)
    target_compile_options(p4_fm_mode_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(p4_fm_mode_test PRIVATE oneseg_iq_engine Threads::Threads)
    add_test(NAME p4_fm_mode COMMAND p4_fm_mode_test)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${P4_LORA_EXTRACT}"
        "${P4_LORA_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/p4_probe_tx.inc" tx
        COMMAND_ERROR_IS_FATAL ANY)
    add_executable(p4_probe_tx_test ../broadcast_fm/test_p4_probe_tx.cpp)
    target_include_directories(p4_probe_tx_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_features(p4_probe_tx_test PRIVATE cxx_std_17)
    target_compile_options(p4_probe_tx_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME p4_probe_tx COMMAND p4_probe_tx_test)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${P4_LORA_EXTRACT}"
        "${P4_LORA_RUNTIME}" "${CMAKE_CURRENT_BINARY_DIR}/p4_probe_rx.inc" rx
        COMMAND_ERROR_IS_FATAL ANY)
    add_executable(p4_probe_rx_test ../broadcast_fm/test_p4_probe_rx.cpp)
    target_include_directories(p4_probe_rx_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
        ../../modules/core_sys/include)
    target_compile_features(p4_probe_rx_test PRIVATE cxx_std_17)
    target_compile_options(p4_probe_rx_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME p4_probe_rx COMMAND p4_probe_rx_test)
    add_executable(p4_fm_runtime_test ../broadcast_fm/test_p4_runtime.cpp
        ../../platform/esp/idf_common/src/platform_ui_fm_runtime.cpp)
    target_include_directories(p4_fm_runtime_test PRIVATE
        ../broadcast_fm/p4_runtime_fakes ../../platform/shared/include
        ../../platform/esp/idf_common/include ../../modules/core_sys/include)
    target_compile_definitions(p4_fm_runtime_test PRIVATE ESP_PLATFORM=1 TRAIL_MATE_IDF_RADIO_LR2021=1)
    target_compile_features(p4_fm_runtime_test PRIVATE cxx_std_17)
    target_compile_options(p4_fm_runtime_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(p4_fm_runtime_test PRIVATE Threads::Threads)
    add_test(NAME p4_fm_runtime COMMAND p4_fm_runtime_test)
    set_tests_properties(p4_fm_runtime PROPERTIES TIMEOUT 20)
endif()
option(TRAIL_MATE_TEST_ONESEG_MEDIA "Build native FFmpeg television media tests" OFF)
if(TRAIL_MATE_TEST_ONESEG_MEDIA)
    get_filename_component(MEDIA_REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    find_package(SDL2 REQUIRED)
    include(../../cmake/TrailMateOneSegMedia.cmake)
    trailmate_add_oneseg_media(oneseg_media)
    target_compile_options(oneseg_media PRIVATE -Wall -Wextra -Werror)
    add_executable(oneseg_media_test test_media.cpp)
    add_executable(oneseg_audio_clock_test test_audio_clock.cpp)
    target_include_directories(oneseg_audio_clock_test PRIVATE "${MEDIA_REPO}/platform/linux/common/include")
    target_compile_features(oneseg_audio_clock_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_audio_clock_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(oneseg_audio_clock_test PRIVATE Threads::Threads)
    add_test(NAME oneseg_audio_clock COMMAND oneseg_audio_clock_test)
    target_compile_features(oneseg_media_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_media_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(oneseg_media_test PRIVATE oneseg_media)
    add_executable(oneseg_pcm_test test_pcm.cpp
        "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_pcm_output.cpp"
        "${MEDIA_REPO}/platform/linux/common/src/platform/linux/sdl_pcm_output.cpp"
        "${MEDIA_REPO}/platform/linux/common/src/platform/resource_lock.cpp"
        "${MEDIA_REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp")
    target_include_directories(oneseg_pcm_test PRIVATE
        "${MEDIA_REPO}/platform/linux/common/include" "${MEDIA_REPO}/modules/core_sys/include"
        "${MEDIA_REPO}/modules/core_music/include")
    target_compile_features(oneseg_pcm_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_pcm_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(oneseg_pcm_test PRIVATE Threads::Threads SDL2::SDL2)
    add_test(NAME oneseg_pcm COMMAND oneseg_pcm_test)
    set_tests_properties(oneseg_pcm PROPERTIES TIMEOUT 10 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
    if(TRAIL_MATE_ONESEG_BACKEND_PREFIX)
        add_executable(oneseg_media_pipeline_test test_media_pipeline.cpp
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_decoder_process.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_pipe_sink.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_pcm_output.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/sdl_pcm_output.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/resource_lock.cpp"
            "${MEDIA_REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp")
        target_include_directories(oneseg_media_pipeline_test PRIVATE
            "${MEDIA_REPO}/platform/shared/include" "${MEDIA_REPO}/modules/core_sys/include"
            "${MEDIA_REPO}/modules/core_music/include")
        target_compile_features(oneseg_media_pipeline_test PRIVATE cxx_std_17)
        target_compile_options(oneseg_media_pipeline_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_link_libraries(oneseg_media_pipeline_test PRIVATE oneseg_media SDL2::SDL2)
        add_executable(oneseg_tv_session_test test_tv_session.cpp
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_tv_session.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_receive_worker.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_decoder_process.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_pipe_sink.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/oneseg_pcm_output.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/linux/sdl_pcm_output.cpp"
            "${MEDIA_REPO}/platform/linux/common/src/platform/resource_lock.cpp"
            "${MEDIA_REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp")
        target_include_directories(oneseg_tv_session_test PRIVATE
            "${MEDIA_REPO}/platform/shared/include" "${MEDIA_REPO}/modules/core_sys/include"
            "${MEDIA_REPO}/modules/core_music/include")
        target_compile_features(oneseg_tv_session_test PRIVATE cxx_std_17)
        target_compile_options(oneseg_tv_session_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_link_libraries(oneseg_tv_session_test PRIVATE oneseg_media SDL2::SDL2)
        add_test(NAME oneseg_tv_metadata COMMAND oneseg_tv_session_test "${CMAKE_CURRENT_SOURCE_DIR}/decoder_child_fixture.py")
        set_tests_properties(oneseg_tv_metadata PROPERTIES TIMEOUT 30 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
        get_target_property(oneseg_session_sources oneseg_tv_session_test SOURCES)
        list(REMOVE_ITEM oneseg_session_sources test_tv_session.cpp)
        add_executable(oneseg_tv_programmes_test test_tv_programmes.cpp ${oneseg_session_sources})
        target_include_directories(oneseg_tv_programmes_test PRIVATE
            "${MEDIA_REPO}/platform/shared/include" "${MEDIA_REPO}/modules/core_sys/include"
            "${MEDIA_REPO}/modules/core_music/include")
        target_compile_features(oneseg_tv_programmes_test PRIVATE cxx_std_17)
        target_compile_options(oneseg_tv_programmes_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_link_libraries(oneseg_tv_programmes_test PRIVATE oneseg_media SDL2::SDL2)
    endif()
endif()
add_executable(oneseg_programs_test test_programs.cpp)
target_compile_features(oneseg_programs_test PRIVATE cxx_std_17)
target_include_directories(oneseg_programs_test PRIVATE ../../platform/shared/include)
if(MSVC)
    target_compile_options(oneseg_programs_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(oneseg_programs_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME oneseg_programs COMMAND oneseg_programs_test)
add_executable(oneseg_lr2021_rx_test test_lr2021_rx.cpp)
target_compile_features(oneseg_lr2021_rx_test PRIVATE cxx_std_17)
target_include_directories(oneseg_lr2021_rx_test PRIVATE ../../platform/linux/common/include)
if(MSVC)
    target_compile_options(oneseg_lr2021_rx_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(oneseg_lr2021_rx_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME oneseg_lr2021_rx COMMAND oneseg_lr2021_rx_test)
include(../../cmake/TrailMateLr2021Iq.cmake)
trailmate_add_lr2021_iq(oneseg_iq_engine)
add_executable(fm_lr2021_stream_test ../broadcast_fm/test_lr2021_stream.cpp)
target_include_directories(fm_lr2021_stream_test PRIVATE ../../platform/shared/include)
target_compile_features(fm_lr2021_stream_test PRIVATE cxx_std_17)
target_link_libraries(fm_lr2021_stream_test PRIVATE oneseg_iq_engine)
if(MSVC)
    target_compile_options(fm_lr2021_stream_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(fm_lr2021_stream_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME fm_lr2021_stream COMMAND fm_lr2021_stream_test)
add_executable(oneseg_stream_test test_stream.cpp)
target_include_directories(oneseg_stream_test PRIVATE
    ../../platform/linux/common/include ../../platform/linux/common/src/platform/linux)
target_link_libraries(oneseg_stream_test PRIVATE oneseg_iq_engine)
target_compile_features(oneseg_stream_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(oneseg_stream_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(oneseg_stream_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME oneseg_stream COMMAND oneseg_stream_test)
set_tests_properties(oneseg_stream PROPERTIES TIMEOUT 5)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(TRAIL_MATE_ONESEG_BACKEND_PREFIX "" CACHE PATH "Optional native private gr-isdbt test prefix")
set(TRAIL_MATE_ONESEG_RECORDED_IQ "" CACHE FILEPATH "Optional offline signed IQ8 recording for native TMCC regression")
add_test(NAME oneseg_tmcc_parameters COMMAND "${Python3_EXECUTABLE}"
    "${REPO}/apps/linux_lvgl/tests/oneseg_tmcc_parameters_test.py")
if(UNIX AND TRAIL_MATE_ONESEG_BACKEND_PREFIX)
    add_test(NAME oneseg_ofdm_candidates COMMAND "${Python3_EXECUTABLE}"
        "${REPO}/apps/linux_lvgl/tests/oneseg_ofdm_acquisition_test.py")
    add_test(NAME oneseg_decoder COMMAND "${Python3_EXECUTABLE}"
        "${REPO}/apps/linux_lvgl/tests/oneseg_decoder_test.py"
        --decoder "${REPO}/apps/linux_lvgl/tools/oneseg_decode.py"
        --backend-prefix "${TRAIL_MATE_ONESEG_BACKEND_PREFIX}"
        --artifact-dir "${CMAKE_CURRENT_BINARY_DIR}/decoder-review")
    set_tests_properties(oneseg_decoder PROPERTIES TIMEOUT 120)
    if(TRAIL_MATE_ONESEG_RECORDED_IQ)
        add_test(NAME oneseg_auto_decoder COMMAND "${Python3_EXECUTABLE}"
            "${REPO}/apps/linux_lvgl/tests/oneseg_auto_decoder_test.py"
            --decoder "${REPO}/apps/linux_lvgl/tools/oneseg_decode.py"
            --backend-prefix "${TRAIL_MATE_ONESEG_BACKEND_PREFIX}"
            --recorded-iq "${TRAIL_MATE_ONESEG_RECORDED_IQ}"
            --transport-inspector "$<TARGET_FILE:oneseg_transport_inspect>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/auto-decoder-review")
        set_tests_properties(oneseg_auto_decoder PROPERTIES TIMEOUT 100)
        add_test(NAME oneseg_ofdm_acquisition COMMAND "${Python3_EXECUTABLE}"
            "${REPO}/apps/linux_lvgl/tests/oneseg_ofdm_acquisition_test.py"
            --backend-prefix "${TRAIL_MATE_ONESEG_BACKEND_PREFIX}"
            --recorded-iq "${TRAIL_MATE_ONESEG_RECORDED_IQ}"
            --transport-inspector "$<TARGET_FILE:oneseg_transport_inspect>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/ofdm-acquisition-report.json")
        set_tests_properties(oneseg_ofdm_acquisition PROPERTIES TIMEOUT 60)
        add_test(NAME oneseg_tmcc_native COMMAND "${Python3_EXECUTABLE}"
            "${REPO}/apps/linux_lvgl/tests/oneseg_tmcc_native_test.py"
            --decoder "${REPO}/apps/linux_lvgl/tools/oneseg_decode.py"
            --backend-prefix "${TRAIL_MATE_ONESEG_BACKEND_PREFIX}"
            --recorded-iq "${TRAIL_MATE_ONESEG_RECORDED_IQ}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/tmcc-review")
        set_tests_properties(oneseg_tmcc_native PROPERTIES TIMEOUT 100)
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${REPO}/platform/linux/common/src/platform/linux/lr2021_radio.cpp"
    "${REPO}/platform/linux/common/src/platform/linux/linux_radiolib_hal.cpp"
    extract_transport.py)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_transport.py"
    "${REPO}" "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
add_executable(oneseg_transport_test test_transport.cpp)
target_include_directories(oneseg_transport_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(oneseg_transport_test PRIVATE oneseg_iq_engine)
target_compile_features(oneseg_transport_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(oneseg_transport_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(oneseg_transport_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME oneseg_transport COMMAND oneseg_transport_test)
add_executable(oneseg_rf_switch_test test_rf_switch.cpp)
target_compile_features(oneseg_rf_switch_test PRIVATE cxx_std_17)
target_include_directories(oneseg_rf_switch_test PRIVATE ../../platform/linux/common/include)
if(MSVC)
    target_compile_options(oneseg_rf_switch_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(oneseg_rf_switch_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME oneseg_rf_switch COMMAND oneseg_rf_switch_test)
add_executable(oneseg_rf_lifecycle_test test_rf_lifecycle.cpp)
target_include_directories(oneseg_rf_lifecycle_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    ../../platform/linux/common/include)
if(WIN32 AND MINGW)
    # Sequential harness; this legacy MinGW lacks gthreads. Linux uses real mutex.
    target_include_directories(oneseg_rf_lifecycle_test PRIVATE ../p4_hardware/fakes)
endif()
target_link_libraries(oneseg_rf_lifecycle_test PRIVATE oneseg_iq_engine)
target_compile_features(oneseg_rf_lifecycle_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(oneseg_rf_lifecycle_test PRIVATE /W4 /UNDEBUG)
else()
    target_compile_options(oneseg_rf_lifecycle_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME oneseg_rf_lifecycle COMMAND oneseg_rf_lifecycle_test)
if(UNIX)
    find_package(Threads REQUIRED)
    add_executable(oneseg_transport_inspect inspect_transport.cpp)
    add_executable(oneseg_service_information_test test_service_information.cpp)
    target_include_directories(oneseg_service_information_test PRIVATE ../../platform/linux/common/include)
    target_compile_features(oneseg_service_information_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_service_information_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME oneseg_service_information COMMAND oneseg_service_information_test)
    set(TRAIL_MATE_ONESEG_ARIB_PREFIX "" CACHE PATH "Private native ARIB character library prefix")
    if(TRAIL_MATE_ONESEG_ARIB_PREFIX)
        find_path(ONESEG_ARIB_INCLUDE aribb24/decoder.h PATHS "${TRAIL_MATE_ONESEG_ARIB_PREFIX}/usr/include" NO_DEFAULT_PATH REQUIRED)
        find_library(ONESEG_ARIB_LIBRARY aribb24 PATHS "${TRAIL_MATE_ONESEG_ARIB_PREFIX}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" NO_DEFAULT_PATH REQUIRED)
    elseif(TRAIL_MATE_TEST_ONESEG_MEDIA)
        find_path(ONESEG_ARIB_INCLUDE aribb24/decoder.h REQUIRED)
        find_library(ONESEG_ARIB_LIBRARY aribb24 REQUIRED)
    endif()
    if(ONESEG_ARIB_INCLUDE AND ONESEG_ARIB_LIBRARY)
        add_executable(oneseg_arib_text_test test_arib_text.cpp
            "${REPO}/platform/linux/common/src/platform/linux/oneseg_arib_text.cpp")
        target_include_directories(oneseg_arib_text_test PRIVATE ../../platform/linux/common/include "${ONESEG_ARIB_INCLUDE}")
        target_compile_features(oneseg_arib_text_test PRIVATE cxx_std_17)
        target_compile_options(oneseg_arib_text_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_link_libraries(oneseg_arib_text_test PRIVATE "${ONESEG_ARIB_LIBRARY}")
        add_test(NAME oneseg_arib_text COMMAND oneseg_arib_text_test)
    endif()
    target_include_directories(oneseg_transport_inspect PRIVATE ../../platform/shared/include)
    target_compile_features(oneseg_transport_inspect PRIVATE cxx_std_17)
    target_compile_options(oneseg_transport_inspect PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_executable(oneseg_channel_store_test test_channel_store.cpp)
    target_include_directories(oneseg_channel_store_test PRIVATE ../../platform/linux/common/include)
    target_compile_features(oneseg_channel_store_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_channel_store_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME oneseg_channel_store COMMAND oneseg_channel_store_test)
    add_executable(oneseg_channel_name_test test_channel_name.cpp)
    target_include_directories(oneseg_channel_name_test PRIVATE ../../platform/linux/common/include)
    target_compile_features(oneseg_channel_name_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_channel_name_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME oneseg_channel_name COMMAND oneseg_channel_name_test)
    add_executable(oneseg_event_schedule_test test_event_schedule.cpp)
    target_include_directories(oneseg_event_schedule_test PRIVATE ../../platform/linux/common/include)
    target_compile_features(oneseg_event_schedule_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_event_schedule_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME oneseg_event_schedule COMMAND oneseg_event_schedule_test)
    add_executable(oneseg_scan_controller_test test_scan_controller.cpp)
    target_include_directories(oneseg_scan_controller_test PRIVATE ../../platform/linux/common/include)
    target_compile_features(oneseg_scan_controller_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_scan_controller_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME oneseg_scan_controller COMMAND oneseg_scan_controller_test)
    add_executable(oneseg_scan_observation_test test_scan_observation.cpp)
    target_include_directories(oneseg_scan_observation_test PRIVATE
        ../../platform/linux/common/include ../../platform/shared/include)
    target_compile_features(oneseg_scan_observation_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_scan_observation_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME oneseg_scan_observation COMMAND oneseg_scan_observation_test)
    set(LINUX_SRC "${REPO}/platform/linux/common/src")
    add_executable(oneseg_worker_test test_worker.cpp
        "${LINUX_SRC}/platform/linux/oneseg_receive_worker.cpp"
        "${LINUX_SRC}/platform/linux/oneseg_pipe_sink.cpp"
        "${LINUX_SRC}/platform/ui/oneseg_iq_runtime.cpp")
    add_executable(oneseg_pipe_test test_pipe.cpp "${LINUX_SRC}/platform/linux/oneseg_pipe_sink.cpp")
    add_executable(oneseg_process_test test_process.cpp
        "${LINUX_SRC}/platform/linux/oneseg_pipe_sink.cpp"
        "${LINUX_SRC}/platform/linux/oneseg_receive_worker.cpp"
        "${LINUX_SRC}/platform/linux/oneseg_decoder_process.cpp")
    target_include_directories(oneseg_process_test PRIVATE
        ../../platform/linux/common/include ../../platform/shared/include)
    target_compile_features(oneseg_process_test PRIVATE cxx_std_17)
    target_compile_options(oneseg_process_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(oneseg_process_test PRIVATE Threads::Threads)
    add_test(NAME oneseg_process COMMAND oneseg_process_test "${CMAKE_CURRENT_SOURCE_DIR}/decoder_child_fixture.py")
    set_tests_properties(oneseg_process PROPERTIES TIMEOUT 30)
    if(TRAIL_MATE_ONESEG_BACKEND_PREFIX)
        add_test(NAME oneseg_process_native COMMAND oneseg_process_test
            "${CMAKE_CURRENT_SOURCE_DIR}/decoder_child_fixture.py"
            "${REPO}/apps/linux_lvgl/tools/oneseg_decode.py" "${TRAIL_MATE_ONESEG_BACKEND_PREFIX}")
        set_tests_properties(oneseg_process_native PROPERTIES TIMEOUT 40)
        if(TRAIL_MATE_ONESEG_RECORDED_IQ)
            add_test(NAME oneseg_process_native_recorded COMMAND oneseg_process_test
                "${CMAKE_CURRENT_SOURCE_DIR}/decoder_child_fixture.py"
                "${REPO}/apps/linux_lvgl/tools/oneseg_decode.py" "${TRAIL_MATE_ONESEG_BACKEND_PREFIX}"
                "${TRAIL_MATE_ONESEG_RECORDED_IQ}")
            set_tests_properties(oneseg_process_native_recorded PROPERTIES TIMEOUT 40)
        endif()
    endif()
    foreach(kind worker pipe)
        target_include_directories(oneseg_${kind}_test PRIVATE
            ../../platform/linux/common/include ../../modules/core_sys/include)
        target_compile_features(oneseg_${kind}_test PRIVATE cxx_std_17)
        target_compile_options(oneseg_${kind}_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_link_libraries(oneseg_${kind}_test PRIVATE Threads::Threads)
        add_test(NAME oneseg_${kind} COMMAND oneseg_${kind}_test)
        set_tests_properties(oneseg_${kind} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
