cmake_policy(SET CMP0077 NEW)

set(MSDF_ATLAS_BUILD_STANDALONE OFF)
set(MSDF_ATLAS_USE_SKIA OFF)
set(MSDF_ATLAS_NO_ARTERY_FONT ON)
set(MSDFGEN_DISABLE_PNG ON)

set(SDL2MIXER_DEPS_SHARED OFF)
set(SDL2MIXER_VENDORED ON)
set(SDL2MIXER_FLAC OFF)
set(SDL2MIXER_MOD OFF)
set(SDL2MIXER_MP3 OFF)
set(SDL2MIXER_MIDI OFF)
set(SDL2MIXER_OPUS OFF)
set(SDL2MIXER_VORBIS "VORBISFILE")
set(SDL2MIXER_WAVPACK OFF)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(PLUME_SDL_VULKAN_ENABLED ON CACHE BOOL "")
endif()

if (WIN32)
    set(PLUME_D3D12_AGILITY_SDK_ENABLED ON CACHE BOOL "")
endif()

if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(BASE_ARCHITECTURE "${CMAKE_OSX_ARCHITECTURES}")
elseif (CMAKE_SYSTEM_PROCESSOR)
    set(BASE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(BASE_ARCHITECTURE "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

# Next, match common architecture strings down to a known common value.
if (BASE_ARCHITECTURE MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    set(ARCHITECTURE "x86_64")
elseif (BASE_ARCHITECTURE MATCHES "(aarch64)|(AARCH64)|(arm64)|(ARM64)")
    set(ARCHITECTURE "arm64")
else()
    message(FATAL_ERROR "Unsupported CPU architecture: ${BASE_ARCHITECTURE}")
endif()

add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/msdf-atlas-gen")
add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/o1heap")

if (MARATHON_RECOMP_SWITCH)
    if (NOT DEFINED DEVKITPRO)
        set(DEVKITPRO "/opt/devkitpro")
    endif()

    set(SWITCH_PORTLIBS_ROOT "${DEVKITPRO}/portlibs/switch")
    set(SWITCH_PORTLIBS_INCLUDE "${SWITCH_PORTLIBS_ROOT}/include")
    set(SWITCH_PORTLIBS_LIB "${SWITCH_PORTLIBS_ROOT}/lib")

    if (NOT TARGET SDL2::SDL2-static)
        add_library(SDL2::SDL2-static STATIC IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2-static PROPERTIES
            IMPORTED_LOCATION "${SWITCH_PORTLIBS_LIB}/libSDL2.a"
            INTERFACE_INCLUDE_DIRECTORIES "${SWITCH_PORTLIBS_INCLUDE}/SDL2"
            # No EGL/GLESv2/glapi: those portlibs are old Mesa and their
            # bundled ralloc/hash_table clash with the NVK driver's Mesa.
            # SDL's egl* references are satisfied by os/switch/egl_stub.cpp.
            INTERFACE_LINK_LIBRARIES "drm_nouveau;nx;pthread"
        )
    endif()

    if (NOT TARGET SDL2_mixer)
        add_library(SDL2_mixer STATIC IMPORTED GLOBAL)
        set_target_properties(SDL2_mixer PROPERTIES
            IMPORTED_LOCATION "${SWITCH_PORTLIBS_LIB}/libSDL2_mixer.a"
            INTERFACE_INCLUDE_DIRECTORIES "${SWITCH_PORTLIBS_INCLUDE}/SDL2"
            INTERFACE_LINK_LIBRARIES "SDL2::SDL2-static;vorbisidec;modplug;mpg123;opusfile;opus;ogg"
        )
    endif()

    if (NOT TARGET nfd::nfd)
        add_library(nfd_switch_stub STATIC
            "${CMAKE_SOURCE_DIR}/MarathonRecomp/os/switch/nfd_switch_stub.cpp"
        )
        target_include_directories(nfd_switch_stub PUBLIC
            "${MARATHON_RECOMP_THIRDPARTY_ROOT}/nativefiledialog-extended/src/include"
        )
        add_library(nfd::nfd ALIAS nfd_switch_stub)
    endif()

    # ffmpeg: our own cross-built FFmpeg 7.1 with the custom XMAFRAMES decoder
    # (thirdparty/ffmpeg-core/switch, built by switch-build/configure-switch.sh).
    # The switch-ffmpeg portlib lacks the XMAFRAMES decoder MarathonRecomp needs,
    # so we can't use it. This is a minimal avcodec+avutil build (xmaframes +
    # wmapro decoders only), self-contained apart from libc/libm/pthread/libnx.
    if (NOT TARGET ffmpeg)
        set(SWITCH_FFMPEG_ROOT "${MARATHON_RECOMP_THIRDPARTY_ROOT}/ffmpeg-core/switch")
        add_library(ffmpeg INTERFACE)
        target_include_directories(ffmpeg INTERFACE "${SWITCH_FFMPEG_ROOT}/include")
        target_link_libraries(ffmpeg INTERFACE
            "${SWITCH_FFMPEG_ROOT}/lib/libavcodec.a"
            "${SWITCH_FFMPEG_ROOT}/lib/libavutil.a"
            m
            nx
            pthread
        )
    endif()
else()
    add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/nativefiledialog-extended")
    add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/SDL")
    add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/SDL_mixer")
    add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/ffmpeg-core")
endif()

add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/plume")

if (APPLE)
    add_subdirectory("${MARATHON_RECOMP_THIRDPARTY_ROOT}/MoltenVK")
endif()
