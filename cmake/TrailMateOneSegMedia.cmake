include_guard(GLOBAL)

# Optional native media component. It never downloads dependencies during CMake.
# A private development prefix is useful for host validation; device packaging
# should provide matching native FFmpeg development/runtime packages.
set(TRAIL_MATE_ONESEG_FFMPEG_PREFIX "" CACHE PATH "Private native FFmpeg development prefix")
set(TRAIL_MATE_ONESEG_ARIB_PREFIX "" CACHE PATH "Private native ARIB character library prefix")
function(trailmate_add_oneseg_media target)
    if(TARGET ${target})
        return()
    endif()
    find_package(Threads REQUIRED)
    if(TRAIL_MATE_ONESEG_ARIB_PREFIX)
        find_path(TRAIL_MATE_ONESEG_ARIB_INCLUDE aribb24/decoder.h
            PATHS "${TRAIL_MATE_ONESEG_ARIB_PREFIX}/usr/include" NO_DEFAULT_PATH REQUIRED)
        find_library(TRAIL_MATE_ONESEG_ARIB_LIBRARY aribb24
            PATHS "${TRAIL_MATE_ONESEG_ARIB_PREFIX}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" NO_DEFAULT_PATH REQUIRED)
    else()
        find_path(TRAIL_MATE_ONESEG_ARIB_INCLUDE aribb24/decoder.h REQUIRED)
        find_library(TRAIL_MATE_ONESEG_ARIB_LIBRARY aribb24 REQUIRED)
    endif()
    set(media_libraries "")
    if(TRAIL_MATE_ONESEG_FFMPEG_PREFIX)
        find_path(TRAIL_MATE_ONESEG_AV_INCLUDE libavformat/avformat.h
            PATHS "${TRAIL_MATE_ONESEG_FFMPEG_PREFIX}/usr/include/${CMAKE_LIBRARY_ARCHITECTURE}"
                  "${TRAIL_MATE_ONESEG_FFMPEG_PREFIX}/usr/include" NO_DEFAULT_PATH REQUIRED)
        foreach(part avformat avcodec avutil swresample swscale)
            find_library(TRAIL_MATE_ONESEG_${part}_LIBRARY NAMES ${part}
                PATHS "${TRAIL_MATE_ONESEG_FFMPEG_PREFIX}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
                      "${TRAIL_MATE_ONESEG_FFMPEG_PREFIX}/usr/lib" NO_DEFAULT_PATH REQUIRED)
            list(APPEND media_libraries "${TRAIL_MATE_ONESEG_${part}_LIBRARY}")
        endforeach()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(TRAIL_MATE_ONESEG_FFMPEG REQUIRED IMPORTED_TARGET
            libavformat>=59 libavcodec>=59 libavutil>=57 libswresample>=4 libswscale>=6)
        list(APPEND media_libraries PkgConfig::TRAIL_MATE_ONESEG_FFMPEG)
    endif()
    get_filename_component(media_repo "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_library(${target} STATIC
        "${media_repo}/platform/linux/common/src/platform/linux/oneseg_arib_text.cpp"
        "${media_repo}/platform/linux/common/src/platform/linux/oneseg_media_worker.cpp")
    target_include_directories(${target} PUBLIC "${media_repo}/platform/linux/common/include")
    target_include_directories(${target} PRIVATE "${TRAIL_MATE_ONESEG_ARIB_INCLUDE}")
    if(TRAIL_MATE_ONESEG_FFMPEG_PREFIX)
        target_include_directories(${target} PRIVATE "${TRAIL_MATE_ONESEG_AV_INCLUDE}")
    endif()
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_link_libraries(${target} PUBLIC ${media_libraries} "${TRAIL_MATE_ONESEG_ARIB_LIBRARY}" Threads::Threads)
endfunction()

# The CM0 cross-package cannot use host dpkg-shlibdeps. Read the target libraries
# selected by CMake rather than guessing Debian FFmpeg package ABI numbers.
function(trailmate_oneseg_debian_dependencies output)
    if(NOT CMAKE_READELF)
        find_program(CMAKE_READELF NAMES readelf REQUIRED)
    endif()
    set(dependencies "trailmate-oneseg-backend (>= 1.1.0), trailmate-oneseg-backend-cm0-abi-v1, trailmate-oneseg-backend-auto-v1, libaribb24-0t64")
    if(NOT TRAIL_MATE_ONESEG_ARIB_LIBRARY)
        find_library(TRAIL_MATE_ONESEG_ARIB_LIBRARY aribb24 REQUIRED)
    endif()
    execute_process(COMMAND "${CMAKE_READELF}" -d "${TRAIL_MATE_ONESEG_ARIB_LIBRARY}"
        OUTPUT_VARIABLE arib_dynamic ERROR_VARIABLE arib_diagnostic RESULT_VARIABLE arib_result)
    if(NOT arib_result EQUAL 0 OR NOT arib_dynamic MATCHES "SONAME[^\n]*\\[libaribb24\\.so\\.0\\]")
        message(FATAL_ERROR "Cannot determine Debian ARIB dependency from ${TRAIL_MATE_ONESEG_ARIB_LIBRARY}: ${arib_diagnostic}")
    endif()
    foreach(part avformat avcodec avutil swresample swscale)
        if(NOT TRAIL_MATE_ONESEG_${part}_LIBRARY)
            find_library(TRAIL_MATE_ONESEG_${part}_LIBRARY NAMES ${part} REQUIRED)
        endif()
        execute_process(COMMAND "${CMAKE_READELF}" -d "${TRAIL_MATE_ONESEG_${part}_LIBRARY}"
            OUTPUT_VARIABLE dynamic ERROR_VARIABLE diagnostic RESULT_VARIABLE result)
        if(NOT result EQUAL 0 OR NOT dynamic MATCHES "SONAME[^\n]*\\[lib${part}\\.so\\.([0-9]+)\\]")
            message(FATAL_ERROR "Cannot determine Debian 1seg dependency from ${TRAIL_MATE_ONESEG_${part}_LIBRARY}: ${diagnostic}")
        endif()
        string(APPEND dependencies ", lib${part}${CMAKE_MATCH_1}")
    endforeach()
    set(${output} "${dependencies}" PARENT_SCOPE)
endfunction()
