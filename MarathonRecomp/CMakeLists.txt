project("MarathonRecomp")

if (MARATHON_RECOMP_SWITCH)
    option(MARATHON_RECOMP_SWITCH_USE_PREGENERATED_APP_SHADERS "Use pre-generated SPIR-V headers for built-in app shaders." OFF)
endif()

if (WIN32)
    option(MARATHON_RECOMP_D3D12 "Add D3D12 support for rendering" ON)
endif()

if (APPLE)
    option(MARATHON_RECOMP_METAL "Add Metal support for rendering" ON)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    option(MARATHON_RECOMP_FLATPAK "Configure the build for Flatpak compatibility." OFF)
endif()

function(BIN2C)
    cmake_parse_arguments(BIN2C_ARGS "" "TARGET_OBJ;SOURCE_FILE;DEST_FILE;ARRAY_NAME;COMPRESSION_TYPE" "" ${ARGN})

    if(NOT BIN2C_ARGS_TARGET_OBJ)
        message(FATAL_ERROR "TARGET_OBJ not specified.")
    endif()

    if(NOT BIN2C_ARGS_SOURCE_FILE)
        message(FATAL_ERROR "SOURCE_FILE not specified.")
    endif()

    if(NOT BIN2C_ARGS_DEST_FILE)
        set(BIN2C_ARGS_DEST_FILE "${BIN2C_ARGS_SOURCE_FILE}")
    endif()

    if(NOT BIN2C_ARGS_COMPRESSION_TYPE)
        set(BIN2C_ARGS_COMPRESSION_TYPE "none")
    endif()

    if (MARATHON_RECOMP_SWITCH)
        set(BIN2C_COMMAND "${MARATHON_RECOMP_HOST_FILE_TO_C}")
    else()
        set(BIN2C_COMMAND "$<TARGET_FILE:file_to_c>")
    endif()

    add_custom_command(OUTPUT "${BIN2C_ARGS_DEST_FILE}.c"
        COMMAND ${BIN2C_COMMAND} "${BIN2C_ARGS_SOURCE_FILE}" "${BIN2C_ARGS_ARRAY_NAME}" "${BIN2C_ARGS_COMPRESSION_TYPE}" "${BIN2C_ARGS_DEST_FILE}.c" "${BIN2C_ARGS_DEST_FILE}.h"
        DEPENDS "${BIN2C_ARGS_SOURCE_FILE}" ${BIN2C_COMMAND}
        BYPRODUCTS "${BIN2C_ARGS_DEST_FILE}.h"
        COMMENT "Generating binary header for ${BIN2C_ARGS_SOURCE_FILE}..."
    )

    set_source_files_properties(${BIN2C_ARGS_DEST_FILE}.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_sources(${BIN2C_ARGS_TARGET_OBJ} PRIVATE ${BIN2C_ARGS_DEST_FILE}.c)
endfunction()

add_compile_options(
    -fno-strict-aliasing
    -Wno-switch
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-void-pointer-to-int-cast
    -Wno-int-to-void-pointer-cast
    -Wno-invalid-offsetof
    -Wno-null-arithmetic
    -Wno-null-conversion
    -Wno-tautological-undefined-compare
)

if (WIN32)
    add_compile_options(/fp:strict)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ffp-model=strict>)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-frounding-math>)
endif()

add_compile_definitions(
    SDL_MAIN_HANDLED
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR # Microsoft wtf?
    _CRT_SECURE_NO_WARNINGS)

set(MARATHON_RECOMP_PRECOMPILED_HEADERS
    "stdafx.h"
)

set(MARATHON_RECOMP_KERNEL_CXX_SOURCES
    "kernel/imports.cpp"
    "kernel/xdm.cpp"
    "kernel/heap.cpp"
    "kernel/memory.cpp"
    "kernel/xam.cpp"
    "kernel/io/file_system.cpp"
)

set(MARATHON_RECOMP_LOCALE_CXX_SOURCES
    "locale/achievement_locale.cpp"
    "locale/config_locale.cpp"
    "locale/locale.cpp"
)

if (MARATHON_RECOMP_SWITCH)
    set(MARATHON_RECOMP_OS_CXX_SOURCES
        "os/switch/logger_switch.cpp"
        "os/switch/media_switch.cpp"
        "os/switch/process_switch.cpp"
        "os/switch/runtime_switch.cpp"
        "os/switch/user_switch.cpp"
        "os/switch/version_switch.cpp"
        # Satisfies SDL2's egl* references without linking the old-Mesa
        # libEGL portlib, whose bundled ralloc clashes with the NVK driver.
        "os/switch/egl_stub.cpp"
        # Lazy commit-on-fault: restores PC's zero-page semantics for stray and
        # aliased guest reads that the sparse commit does not cover.
        "os/switch/exception_switch.cpp"
    )
    # Only compile the weak ICD fallback when the real NVK driver is absent: a
    # weak definition in an object satisfies the ICD symbol and would stop the
    # linker pulling the strong one from libvulkan.a.
    if (NOT PLUME_SWITCH_NVK_LIBRARY OR NOT EXISTS "${PLUME_SWITCH_NVK_LIBRARY}")
        list(APPEND MARATHON_RECOMP_OS_CXX_SOURCES "os/switch/nvk_icd_stub.cpp")
    endif()
    # Switch performance and diagnostics modules (docs/SWITCH-PERFORMANCE.md): every source in
    # os/switch/perf is built, so a new module needs no change here. C sources skip the C++ PCH.
    file(GLOB MARATHON_RECOMP_SWITCH_PERF_CXX_SOURCES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/os/switch/perf/*.cpp")
    file(GLOB MARATHON_RECOMP_SWITCH_PERF_C_SOURCES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/os/switch/perf/*.c")
    list(APPEND MARATHON_RECOMP_OS_CXX_SOURCES
        ${MARATHON_RECOMP_SWITCH_PERF_CXX_SOURCES} ${MARATHON_RECOMP_SWITCH_PERF_C_SOURCES})
    if (MARATHON_RECOMP_SWITCH_PERF_C_SOURCES)
        set_source_files_properties(${MARATHON_RECOMP_SWITCH_PERF_C_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
elseif (WIN32)
    set(MARATHON_RECOMP_OS_CXX_SOURCES
        "os/win32/logger_win32.cpp"
        "os/win32/media_win32.cpp"
        "os/win32/process_win32.cpp"
        "os/win32/user_win32.cpp"
        "os/win32/version_win32.cpp"
    )
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(MARATHON_RECOMP_OS_CXX_SOURCES
        "os/linux/logger_linux.cpp"
        "os/linux/media_linux.cpp"
        "os/linux/process_linux.cpp"
        "os/linux/user_linux.cpp"
        "os/linux/version_linux.cpp"
    )
elseif (APPLE)
    set(MARATHON_RECOMP_OS_CXX_SOURCES
        "os/macos/logger_macos.cpp"
        "os/macos/media_macos.cpp"
        "os/macos/process_macos.cpp"
        "os/macos/user_macos.cpp"
        "os/macos/version_macos.cpp"
    )
endif()

set(MARATHON_RECOMP_CPU_CXX_SOURCES
    "cpu/guest_thread.cpp"
)

set(MARATHON_RECOMP_GPU_CXX_SOURCES
    "gpu/video.cpp"
    "gpu/imgui/imgui_common.cpp"
    "gpu/imgui/imgui_font_builder.cpp"
    "gpu/imgui/imgui_snapshot.cpp"
)

set(MARATHON_RECOMP_APU_CXX_SOURCES
    "apu/audio.cpp"
    "apu/xma_decoder.cpp"
    "apu/embedded_player.cpp"
    "apu/driver/sdl2_driver.cpp"
)

set(MARATHON_RECOMP_HID_CXX_SOURCES
    "hid/hid.cpp"
)
if (MARATHON_RECOMP_SWITCH)
    list(APPEND MARATHON_RECOMP_HID_CXX_SOURCES
        "hid/driver/switch_hid.cpp"
    )
else()
    list(APPEND MARATHON_RECOMP_HID_CXX_SOURCES
        "hid/driver/sdl_hid.cpp"
    )
endif()

set(MARATHON_RECOMP_PATCHES_CXX_SOURCES
    "patches/aspect_ratio_patches.cpp"
    "patches/audio_patches.cpp"
    "patches/camera_patches.cpp"
    "patches/fps_patches.cpp"
    "patches/frontend_listener.cpp"
    "patches/input_patches.cpp"
    "patches/loading_patches.cpp"
    "patches/MainMenuTask_patches.cpp"
    "patches/misc_patches.cpp"
    "patches/pause_patches.cpp"
    "patches/player_patches.cpp"
    "patches/SaveDataTask_patches.cpp"
    "patches/text_patches.cpp"
    "patches/TitleTask_patches.cpp"
    "patches/video_patches.cpp"
)

set(MARATHON_RECOMP_UI_CXX_SOURCES
    "ui/achievement_menu.cpp"
    "ui/achievement_overlay.cpp"
    "ui/black_bar.cpp"
    "ui/button_window.cpp"
    "ui/common_menu.cpp"
    "ui/fader.cpp"
    "ui/game_window.cpp"
    "ui/imgui_utils.cpp"
    "ui/installer_wizard.cpp"
    "ui/message_window.cpp"
    "ui/options_menu.cpp"
)

set(MARATHON_RECOMP_INSTALL_CXX_SOURCES
    "install/installer.cpp"
    "install/iso_file_system.cpp"
    "install/update_checker.cpp"
    "install/xcontent_file_system.cpp"
    "install/hashes/game.cpp"
    "install/hashes/episode_sonic.cpp"
    "install/hashes/episode_shadow.cpp"
    "install/hashes/episode_silver.cpp"
    "install/hashes/episode_amigo.cpp"
    "install/hashes/mission_sonic.cpp"
    "install/hashes/mission_shadow.cpp"
    "install/hashes/mission_silver.cpp"
)

set(MARATHON_RECOMP_USER_CXX_SOURCES
    "user/achievement_data.cpp"
    "user/achievement_manager.cpp"
    "user/config.cpp"
    "user/registry.cpp"
    "user/paths.cpp"
)

set(MARATHON_RECOMP_MOD_CXX_SOURCES
    "mod/mod_loader.cpp"
)

set(MARATHON_RECOMP_UTILS_CXX_SOURCES
    "utils/bit_stream.cpp"
    "utils/ring_buffer.cpp"
)

set(MARATHON_RECOMP_THIRDPARTY_SOURCES
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui/backends/imgui_impl_sdl2.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui/imgui.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui/imgui_demo.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui/imgui_draw.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui/imgui_tables.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui/imgui_widgets.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/implot/implot.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/implot/implot_demo.cpp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/implot/implot_items.cpp"
    "${MARATHON_RECOMP_TOOLS_ROOT}/XenosRecomp/thirdparty/smol-v/source/smolv.cpp"
)

set(MARATHON_RECOMP_THIRDPARTY_INCLUDES
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/concurrentqueue"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/ddspp"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/imgui"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/implot"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/json/include"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/magic_enum/include"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/stb"
    "${MARATHON_RECOMP_THIRDPARTY_ROOT}/unordered_dense/include"
    "${MARATHON_RECOMP_TOOLS_ROOT}/bc_diff"
    "${MARATHON_RECOMP_TOOLS_ROOT}/XenosRecomp/thirdparty/smol-v/source"
)

set_source_files_properties(${MARATHON_RECOMP_THIRDPARTY_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(MARATHON_RECOMP_CXX_SOURCES
    "app.cpp"
    "exports.cpp"
    "main.cpp"
    "misc_impl.cpp"
    "preload_executable.cpp"
    "sdl_listener.cpp"
    "stdafx.cpp"
    "version.cpp"
    
    ${MARATHON_RECOMP_KERNEL_CXX_SOURCES}
    ${MARATHON_RECOMP_LOCALE_CXX_SOURCES}
    ${MARATHON_RECOMP_OS_CXX_SOURCES}
    ${MARATHON_RECOMP_CPU_CXX_SOURCES}
    ${MARATHON_RECOMP_GPU_CXX_SOURCES}
    ${MARATHON_RECOMP_APU_CXX_SOURCES}
    ${MARATHON_RECOMP_HID_CXX_SOURCES}
    ${MARATHON_RECOMP_PATCHES_CXX_SOURCES}
    ${MARATHON_RECOMP_UI_CXX_SOURCES}
    ${MARATHON_RECOMP_INSTALL_CXX_SOURCES}
    ${MARATHON_RECOMP_USER_CXX_SOURCES}
    ${MARATHON_RECOMP_UTILS_CXX_SOURCES}
    ${MARATHON_RECOMP_MOD_CXX_SOURCES}
    ${MARATHON_RECOMP_THIRDPARTY_SOURCES}
)

include("version.cmake")

set(VERSION_TXT "${PROJECT_SOURCE_DIR}/res/version.txt")

# Only show Git info and build type if not Release.
set(SHOW_GIT_INFO_AND_BUILD_TYPE 0)
if (NOT ${CMAKE_BUILD_TYPE} MATCHES "Release")
    set(SHOW_GIT_INFO_AND_BUILD_TYPE 1)
endif()

if (MARATHON_RECOMP_METAL)
    set(XCRUN_TOOL "/usr/bin/xcrun")
endif()

GenerateVersionSources(
    OUTPUT_DIR ${PROJECT_SOURCE_DIR}
    VERSION_TXT ${VERSION_TXT}
    H_TEMPLATE "${PROJECT_SOURCE_DIR}/res/version.h.template"
    CXX_TEMPLATE "${PROJECT_SOURCE_DIR}/res/version.cpp.template"
    BUILD_TYPE ${CMAKE_BUILD_TYPE}
    SHOW_GIT_INFO ${SHOW_GIT_INFO_AND_BUILD_TYPE}
    SHOW_BUILD_TYPE ${SHOW_GIT_INFO_AND_BUILD_TYPE}
)

if (WIN32)
    # Create binary version number for Win32 integer attributes.
    CreateVersionString(
        VERSION_TXT ${VERSION_TXT}
        OUTPUT_CSV 1
        OUTPUT_VAR WIN32_VERSION_BINARY
    )

    # Create string version number for Win32 detailed attributes.
    CreateVersionString(
        VERSION_TXT ${VERSION_TXT}
        BUILD_TYPE ${CMAKE_BUILD_TYPE}
        SHOW_GIT_INFO ${SHOW_GIT_INFO_AND_BUILD_TYPE}
        SHOW_BUILD_TYPE ${SHOW_GIT_INFO_AND_BUILD_TYPE}
        OUTPUT_VAR WIN32_VERSION_STRING
    )

    # Set Win32 icon path.
    set(WIN32_ICON_PATH "${PROJECT_SOURCE_DIR}/../MarathonRecompResources/images/game_icon.ico")

    configure_file("res/win32/res.rc.template" "${CMAKE_BINARY_DIR}/res.rc" @ONLY)
    add_executable(MarathonRecomp ${MARATHON_RECOMP_CXX_SOURCES} "${CMAKE_BINARY_DIR}/res.rc")

    # Hide console for release configurations.
    if (${CMAKE_BUILD_TYPE} MATCHES "Release")
        target_link_options(MarathonRecomp PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
elseif (APPLE)
    # Create version number for app bundle.
    CreateVersionString(
            VERSION_TXT ${VERSION_TXT}
            OUTPUT_VAR MACOS_BUNDLE_VERSION
    )

    add_executable(MarathonRecomp MACOSX_BUNDLE
            ${MARATHON_RECOMP_CXX_SOURCES}
            res/macos/game_icon.icns
    )
    set_source_files_properties(res/macos/game_icon.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(MarathonRecomp PROPERTIES
            OUTPUT_NAME "Marathon Recompiled"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/res/macos/MacOSXBundleInfo.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER sonicnext-dev.MarathonRecomp
            MACOSX_BUNDLE_BUNDLE_NAME "Marathon Recompiled"
            MACOSX_BUNDLE_BUNDLE_VERSION ${MACOS_BUNDLE_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${MACOS_BUNDLE_VERSION}
            MACOSX_BUNDLE_ICON_FILE "game_icon.icns"
    )

    # Linking with MoltenVK directly would prevent using the system Vulkan loader to load with debug layers.
    # Instead, copy the MoltenVK dylib to the app bundle along with an ICD file for the loader to find it.
    # In the event the loader is not installed, the MoltenVK dylib can still be picked up directly in the app bundle.
    set(MVK_BUNDLED_PATH "Resources/vulkan/icd.d")
    set(MVK_DST "${CMAKE_CURRENT_BINARY_DIR}/Marathon Recompiled.app/Contents/${MVK_BUNDLED_PATH}")
    set_property(TARGET MarathonRecomp APPEND PROPERTY BUILD_RPATH "@executable_path/../${MVK_BUNDLED_PATH}")

    set(MVK_ICD_SRC "${MARATHON_RECOMP_THIRDPARTY_ROOT}/MoltenVK/MoltenVK/MoltenVK/icd/MoltenVK_icd.json")
    set(MVK_ICD_DST "${MVK_DST}/MoltenVK_icd.json")
    set(MVK_DYLIB_SRC "${CMAKE_BINARY_DIR}/thirdparty/MoltenVK/libMoltenVK.dylib")
    set(MVK_DYLIB_DST "${MVK_DST}/libMoltenVK.dylib")

    add_custom_command(
            OUTPUT ${MVK_DST}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${MVK_DST})
    add_custom_command(
            OUTPUT ${MVK_ICD_DST}
            DEPENDS ${MVK_ICD_SRC} ${MVK_DST}
            COMMAND ${CMAKE_COMMAND} -E copy ${MVK_ICD_SRC} ${MVK_ICD_DST})
    add_custom_command(
            OUTPUT ${MVK_DYLIB_DST}
            DEPENDS ${MVK_DYLIB_SRC} ${MVK_DST}
            COMMAND ${CMAKE_COMMAND} -E copy ${MVK_DYLIB_SRC} ${MVK_DYLIB_DST})
    add_custom_target(CopyMoltenVK DEPENDS ${MVK_ICD_DST} ${MVK_DYLIB_DST})
    add_dependencies(CopyMoltenVK MoltenVK)
    add_dependencies(MarathonRecomp CopyMoltenVK)
else()
    add_executable(MarathonRecomp ${MARATHON_RECOMP_CXX_SOURCES})
endif()

if (MARATHON_RECOMP_FLATPAK)
    target_compile_definitions(MarathonRecomp PRIVATE 
        "MARATHON_RECOMP_FLATPAK"
        "GAME_INSTALL_DIRECTORY=\"/var/data\""
    )
endif()

if (MARATHON_RECOMP_SWITCH)
    set(MARATHON_RECOMP_HOST_TOOLS_DIR "${CMAKE_SOURCE_DIR}/build/host-tools" CACHE PATH "Directory containing host-side generation tools.")
    set(MARATHON_RECOMP_HOST_FILE_TO_C "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/file_to_c/file_to_c${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host file_to_c executable.")
    set(MARATHON_RECOMP_HOST_U8EXTRACT "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/u8extract/u8extract${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host u8extract executable.")
    set(MARATHON_RECOMP_HOST_XENON_RECOMP "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/XenonRecomp/XenonRecomp/XenonRecomp${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host XenonRecomp executable.")
    set(MARATHON_RECOMP_HOST_XENOS_RECOMP "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/XenosRecomp/XenosRecomp/XenosRecomp${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host XenosRecomp executable.")

    target_compile_definitions(MarathonRecomp PRIVATE
        "MARATHON_RECOMP_SWITCH"
        "__SWITCH__"
        "NX"
        "VK_USE_PLATFORM_VI_NN"
        "IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS"
        "GAME_INSTALL_DIRECTORY=\"sdmc:/switch/MarathonRecomp\""
    )
endif()

find_package(CURL REQUIRED)

if (MARATHON_RECOMP_SWITCH)
    set(MARATHON_RECOMP_HOST_DXC_ROOT "${MARATHON_RECOMP_TOOLS_ROOT}/XenosRecomp/thirdparty/dxc-bin")
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        # x64 dxc: the arm64 build emits invalid SPIR-V for atomic shaders.
        set(DIRECTX_DXC_TOOL "${MARATHON_RECOMP_HOST_DXC_ROOT}/bin/x64/dxc.exe")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(DIRECTX_DXC_TOOL
            "${CMAKE_COMMAND}" "-E" "env" "DYLD_LIBRARY_PATH=${MARATHON_RECOMP_HOST_DXC_ROOT}/lib/x64"
            "${MARATHON_RECOMP_HOST_DXC_ROOT}/bin/x64/dxc-macos"
        )
    else()
        set(DIRECTX_DXC_TOOL
            "${CMAKE_COMMAND}" "-E" "env" "LD_LIBRARY_PATH=${MARATHON_RECOMP_HOST_DXC_ROOT}/lib/x64"
            "${MARATHON_RECOMP_HOST_DXC_ROOT}/bin/x64/dxc-linux"
        )
    endif()
endif()

if (MARATHON_RECOMP_METAL)
    target_compile_definitions(MarathonRecomp PRIVATE MARATHON_RECOMP_METAL)
endif()

if (MARATHON_RECOMP_D3D12)
    find_package(directx-headers CONFIG REQUIRED)
    find_package(directx12-agility CONFIG REQUIRED)
    target_compile_definitions(MarathonRecomp PRIVATE MARATHON_RECOMP_D3D12)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/D3D12)
    add_custom_command(TARGET MarathonRecomp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Microsoft::DirectX12-Core,IMPORTED_LOCATION_RELEASE> $<TARGET_FILE_DIR:MarathonRecomp>/D3D12
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Microsoft::DirectX12-Layers,IMPORTED_LOCATION_DEBUG> $<TARGET_FILE_DIR:MarathonRecomp>/D3D12
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Microsoft::DirectXShaderCompiler,IMPORTED_LOCATION> $<TARGET_FILE_DIR:MarathonRecomp>
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Microsoft::DXIL,IMPORTED_LOCATION> $<TARGET_FILE_DIR:MarathonRecomp>
            COMMAND_EXPAND_LISTS
    )

    target_link_libraries(MarathonRecomp PRIVATE
            Microsoft::DirectXShaderCompiler
            Microsoft::DXIL
            dxgi
    )
endif()

if (WIN32)
    target_link_libraries(MarathonRecomp PRIVATE
        comctl32
        dwmapi
        ntdll
        Shcore
        Synchronization
        winmm
        windowsapp
    )
endif()

target_link_libraries(MarathonRecomp PRIVATE
    fmt::fmt
    libzstd_static
    msdf-atlas-gen::msdf-atlas-gen
    nfd::nfd
    o1heap
    XenonUtils
    SDL2::SDL2-static
    SDL2_mixer
    tomlplusplus::tomlplusplus
    MarathonRecompLib
    xxHash::xxhash
    CURL::libcurl
    plume
)

target_link_libraries(MarathonRecomp PRIVATE ffmpeg)

if (MARATHON_RECOMP_SWITCH)
    # z: the native archive inflate (os/switch/perf/native_inflate.cpp). It used to resolve only through the
    # driver's extra libraries.
    target_link_libraries(MarathonRecomp PRIVATE z nx)

    # Host hooks and the app's own guest-memory accesses can fault into os/switch/exception_switch.cpp as
    # well, which resumes them through x16 (see MarathonRecompLib/CMakeLists.txt). At the link for the code
    # compiled there from LTO objects (lto-wrapper takes -ffixed-* from the link line, not from each object).
    if (MARATHON_RECOMP_SWITCH_FIXED_X16)
        target_compile_options(MarathonRecomp PRIVATE -ffixed-x16)
        target_link_options(MarathonRecomp PRIVATE -ffixed-x16)
    endif()

    # Profile-guided optimisation in two builds from the same build folder (docs/BUILDING-SWITCH.md):
    #   generate: instrumented build; os/switch/perf/pgo_switch.cpp dumps the .gcda files to
    #             sdmc:/switch/MarathonRecomp/pgo (its __wrap_fopen maps MARATHON_RECOMP_SWITCH_PGO_DIR
    #             there). Build it without LTO.
    #   use:      rebuild with those files copied into MARATHON_RECOMP_SWITCH_PGO_DIR.
    # -fprofile-prefix-path makes each .gcda name relative to the build folder, in the host's native
    # form, so the names match between the console and the PC. -fno-profile-values is mandatory:
    # libgcov's value profiler reads TLS through tpidr_el0, which is 0 with libnx's -mtp=soft.
    if (MARATHON_RECOMP_SWITCH_PGO)
        get_filename_component(MARATHON_RECOMP_PGO_DIR "${MARATHON_RECOMP_SWITCH_PGO_DIR}" ABSOLUTE)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" MARATHON_RECOMP_PGO_PREFIX)
        if (MARATHON_RECOMP_SWITCH_PGO STREQUAL "generate")
            if (MARATHON_RECOMP_SWITCH_LTO)
                message(FATAL_ERROR "MARATHON_RECOMP_SWITCH_PGO=generate needs MARATHON_RECOMP_SWITCH_LTO=OFF")
            endif()
            set(MARATHON_RECOMP_PGO_OPTIONS -fprofile-generate=${MARATHON_RECOMP_PGO_DIR}
                "-fprofile-prefix-path=${MARATHON_RECOMP_PGO_PREFIX}" -fprofile-update=single -fno-profile-values)
            target_link_options(MarathonRecomp PRIVATE -fprofile-generate -Wl,--wrap=fopen)
            target_compile_definitions(MarathonRecomp PRIVATE MARATHON_RECOMP_SWITCH_PGO_GENERATE=1
                "MARATHON_RECOMP_SWITCH_PGO_DIR=\"${MARATHON_RECOMP_PGO_DIR}\"")
        elseif (MARATHON_RECOMP_SWITCH_PGO STREQUAL "use")
            # -fprofile-correction: counters updated from several threads without synchronisation
            # (-fprofile-update=single) can come out negative, which GCC otherwise rejects.
            set(MARATHON_RECOMP_PGO_OPTIONS -fprofile-use=${MARATHON_RECOMP_PGO_DIR}
                "-fprofile-prefix-path=${MARATHON_RECOMP_PGO_PREFIX}" -fprofile-partial-training
                -fno-profile-values -fprofile-correction -Wno-missing-profile -Wno-error=coverage-mismatch)
            target_link_options(MarathonRecomp PRIVATE -fprofile-use -fprofile-partial-training -fprofile-correction)
            # GCC reads the .gcda files outside the preprocessor, so no depfile names them and a refreshed
            # profile would rebuild nothing. Their names and contents go into every compile line instead (an
            # unused define), so a new profile rebuilds every object and its LTO IR. A changed, added or
            # removed .gcda reconfigures (CONFIGURE_DEPENDS re-globs at build time).
            file(GLOB_RECURSE MARATHON_RECOMP_PGO_FILES CONFIGURE_DEPENDS "${MARATHON_RECOMP_PGO_DIR}/*.gcda")
            list(SORT MARATHON_RECOMP_PGO_FILES)
            set(MARATHON_RECOMP_PGO_PROFILE_HASHES "")
            foreach(pgo_file IN LISTS MARATHON_RECOMP_PGO_FILES)
                file(SHA256 "${pgo_file}" pgo_file_hash)
                file(RELATIVE_PATH pgo_file_name "${MARATHON_RECOMP_PGO_DIR}" "${pgo_file}")
                string(APPEND MARATHON_RECOMP_PGO_PROFILE_HASHES "${pgo_file_name}=${pgo_file_hash};")
            endforeach()
            if (NOT MARATHON_RECOMP_PGO_FILES)
                message(WARNING "MARATHON_RECOMP_SWITCH_PGO=use: no .gcda files in ${MARATHON_RECOMP_PGO_DIR}")
            endif()
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MARATHON_RECOMP_PGO_FILES})
            string(SHA256 MARATHON_RECOMP_PGO_PROFILE_ID "${MARATHON_RECOMP_PGO_PROFILE_HASHES}")
            string(SUBSTRING "${MARATHON_RECOMP_PGO_PROFILE_ID}" 0 16 MARATHON_RECOMP_PGO_PROFILE_ID)
            list(APPEND MARATHON_RECOMP_PGO_OPTIONS -DMARATHON_RECOMP_SWITCH_PGO_PROFILE_ID=${MARATHON_RECOMP_PGO_PROFILE_ID})
        else()
            message(FATAL_ERROR "MARATHON_RECOMP_SWITCH_PGO must be empty, generate or use (is '${MARATHON_RECOMP_SWITCH_PGO}')")
        endif()
        message(STATUS "Switch: PGO ${MARATHON_RECOMP_SWITCH_PGO} (${MARATHON_RECOMP_PGO_DIR})")
        foreach(pgo_target MarathonRecompLib MarathonRecomp)
            target_compile_options(${pgo_target} PRIVATE ${MARATHON_RECOMP_PGO_OPTIONS})
        endforeach()
    endif()

    # The build ID and the driver ID as a generated header (a -D would not match the precompiled header),
    # rewritten only when its content changes. Include it from the one source file that prints it, not from
    # stdafx.h: the build ID changes with every build of tools/build-switch.sh.
    set(MARATHON_RECOMP_SWITCH_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
    # The SHA-256 of the NVK driver library linked in, which keys the persistent pipeline cache. The configure
    # step runs again when the library changes, so the key follows the driver.
    set(MARATHON_RECOMP_SWITCH_DRIVER_ID "")
    if (PLUME_SWITCH_NVK_LIBRARY AND EXISTS "${PLUME_SWITCH_NVK_LIBRARY}")
        file(SHA256 "${PLUME_SWITCH_NVK_LIBRARY}" MARATHON_RECOMP_SWITCH_DRIVER_ID)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PLUME_SWITCH_NVK_LIBRARY}")
    endif()
    # The SHA-256 of the generated game shader cache, also in the pipeline cache key: when the translator's output
    # changes, the pipelines of the old shaders can never be used again, so the cache starts afresh instead of
    # carrying them until its size cap.
    set(MARATHON_RECOMP_SWITCH_SHADER_ID "")
    set(MARATHON_RECOMP_SWITCH_SHADER_CACHE_SOURCE "${CMAKE_SOURCE_DIR}/MarathonRecompLib/shader/shader_cache.cpp")
    if (EXISTS "${MARATHON_RECOMP_SWITCH_SHADER_CACHE_SOURCE}")
        file(SHA256 "${MARATHON_RECOMP_SWITCH_SHADER_CACHE_SOURCE}" MARATHON_RECOMP_SWITCH_SHADER_ID)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MARATHON_RECOMP_SWITCH_SHADER_CACHE_SOURCE}")
    endif()
    # The compiler options this build was configured with, whatever the build ID says.
    set(MARATHON_RECOMP_SWITCH_BUILD_OPTIONS "")
    if (MARATHON_RECOMP_SWITCH_LTO)
        string(APPEND MARATHON_RECOMP_SWITCH_BUILD_OPTIONS " lto=${MARATHON_RECOMP_SWITCH_LTO_JOBS}")
    endif()
    if (MARATHON_RECOMP_SWITCH_PGO)
        string(APPEND MARATHON_RECOMP_SWITCH_BUILD_OPTIONS " pgo-${MARATHON_RECOMP_SWITCH_PGO}")
        if (MARATHON_RECOMP_PGO_PROFILE_ID)
            string(APPEND MARATHON_RECOMP_SWITCH_BUILD_OPTIONS " pgo_profile=${MARATHON_RECOMP_PGO_PROFILE_ID}")
        endif()
    endif()
    foreach(build_option O2 RECOMP_O2 IPA_PTA CLASSIC_CODEGEN FIXED_X16 PLAIN_GUEST_MEMORY)
        if (MARATHON_RECOMP_SWITCH_${build_option})
            string(TOLOWER "${build_option}" build_option_name)
            string(APPEND MARATHON_RECOMP_SWITCH_BUILD_OPTIONS " ${build_option_name}")
        endif()
    endforeach()
    # Only when it is really used (root CMakeLists.txt: not classic, and ppc/ has the explicit fma).
    if (MARATHON_RECOMP_SWITCH_EXACT_FMA_ACTIVE)
        string(APPEND MARATHON_RECOMP_SWITCH_BUILD_OPTIONS " exact_fma")
    endif()
    if (MARATHON_RECOMP_SWITCH_CODEGEN_OPTIONS)
        string(APPEND MARATHON_RECOMP_SWITCH_BUILD_OPTIONS " ${MARATHON_RECOMP_SWITCH_CODEGEN_OPTIONS}")
    endif()
    string(STRIP "${MARATHON_RECOMP_SWITCH_BUILD_OPTIONS}" MARATHON_RECOMP_SWITCH_BUILD_OPTIONS)
    file(CONFIGURE OUTPUT "${MARATHON_RECOMP_SWITCH_GENERATED_DIR}/switch_build_id.h"
        CONTENT "#pragma once\n#define MARATHON_RECOMP_SWITCH_BUILD_ID \"@MARATHON_RECOMP_SWITCH_BUILD_ID@\"\n#define MARATHON_RECOMP_SWITCH_BUILD_OPTIONS \"@MARATHON_RECOMP_SWITCH_BUILD_OPTIONS@\"\n#define MARATHON_RECOMP_SWITCH_DRIVER_ID \"@MARATHON_RECOMP_SWITCH_DRIVER_ID@\"\n#define MARATHON_RECOMP_SWITCH_SHADER_ID \"@MARATHON_RECOMP_SWITCH_SHADER_ID@\"\n" @ONLY)
    target_include_directories(MarathonRecomp PRIVATE "${MARATHON_RECOMP_SWITCH_GENERATED_DIR}")
endif()

target_include_directories(MarathonRecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/api"
    ${MARATHON_RECOMP_THIRDPARTY_INCLUDES}
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    find_package(X11 REQUIRED)
    pkg_search_module(GLIB REQUIRED glib-2.0)
    pkg_search_module(GIO REQUIRED gio-2.0)
    target_include_directories(MarathonRecomp PRIVATE ${X11_INCLUDE_DIR} ${GLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
    target_link_libraries(MarathonRecomp PRIVATE ${X11_LIBRARIES} ${GLIB_LIBRARIES} ${GIO_LIBRARIES})
endif()

target_precompile_headers(MarathonRecomp PUBLIC ${MARATHON_RECOMP_PRECOMPILED_HEADERS})

function(compile_shader FILE_PATH TARGET_NAME)
    set(HLSL_FILE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/gpu/shader/hlsl/${FILE_PATH}.hlsl)
    set(MSL_FILE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/gpu/shader/msl/${FILE_PATH}.metal)
    cmake_path(GET HLSL_FILE_PATH STEM HLSL_NAME)
    cmake_path(GET MSL_FILE_PATH STEM MSL_NAME)
    if (MARATHON_RECOMP_METAL)
        add_custom_command(
                OUTPUT ${MSL_FILE_PATH}.ir
                COMMAND ${XCRUN_TOOL} -sdk macosx metal -o ${MSL_FILE_PATH}.ir -c ${MSL_FILE_PATH} -D__air__ -DMARATHON_RECOMP -frecord-sources -gline-tables-only
                DEPENDS ${MSL_FILE_PATH}
        )
        add_custom_command(
                OUTPUT ${MSL_FILE_PATH}.metallib
                COMMAND ${XCRUN_TOOL} -sdk macosx metallib -o ${MSL_FILE_PATH}.metallib ${MSL_FILE_PATH}.ir
                DEPENDS ${MSL_FILE_PATH}.ir
        )
        BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${MSL_FILE_PATH}.metallib" DEST_FILE "${MSL_FILE_PATH}.metallib" ARRAY_NAME "g_${MSL_NAME}_air")
    endif()
    if (MARATHON_RECOMP_D3D12)
        add_custom_command(
            OUTPUT ${HLSL_FILE_PATH}.dxil.h
            COMMAND ${DIRECTX_DXC_TOOL} -T ${TARGET_NAME} -HV 2021 -all-resources-bound -Wno-ignored-attributes -DMARATHON_RECOMP -E shaderMain -Fh ${HLSL_FILE_PATH}.dxil.h ${HLSL_FILE_PATH} -Vn g_${HLSL_NAME}_dxil
            DEPENDS ${HLSL_FILE_PATH}
        )
        target_sources(MarathonRecomp PRIVATE ${HLSL_FILE_PATH}.dxil.h)
    endif()
    if (MARATHON_RECOMP_SWITCH AND MARATHON_RECOMP_SWITCH_USE_PREGENERATED_APP_SHADERS)
        if (NOT EXISTS ${HLSL_FILE_PATH}.spirv.h)
            message(FATAL_ERROR "Missing pre-generated app shader header: ${HLSL_FILE_PATH}.spirv.h. Run tools/generate-switch-app-shaders.sh before configuring the Switch build.")
        endif()
        target_sources(MarathonRecomp PRIVATE ${HLSL_FILE_PATH}.spirv.h)
        return()
    endif()
    add_custom_command(
        OUTPUT ${HLSL_FILE_PATH}.spirv.h
        COMMAND ${DIRECTX_DXC_TOOL} -T ${TARGET_NAME} -HV 2021 -all-resources-bound -spirv -fvk-use-dx-layout ${ARGN} -DMARATHON_RECOMP -E shaderMain -Fh ${HLSL_FILE_PATH}.spirv.h ${HLSL_FILE_PATH} -Vn g_${HLSL_NAME}_spirv
        DEPENDS ${HLSL_FILE_PATH}
    )
    target_sources(MarathonRecomp PRIVATE ${HLSL_FILE_PATH}.spirv.h)
endfunction()

function(compile_vertex_shader FILE_PATH)
    compile_shader(${FILE_PATH} vs_6_0 -fvk-invert-y)
endfunction()

function(compile_pixel_shader FILE_PATH)
    compile_shader(${FILE_PATH} ps_6_0)
endfunction()

compile_pixel_shader(blend_color_alpha_ps)
compile_pixel_shader(conditional_survey_ps)
compile_vertex_shader(copy_vs)
compile_pixel_shader(copy_color_ps)
compile_pixel_shader(copy_depth_ps)
compile_pixel_shader(csd_filter_ps)
compile_vertex_shader(csd_no_tex_vs)
compile_vertex_shader(csd_vs)
compile_vertex_shader(enhanced_burnout_blur_vs)
compile_pixel_shader(enhanced_burnout_blur_ps)
compile_pixel_shader(gaussian_blur_3x3)
compile_pixel_shader(gaussian_blur_5x5)
compile_pixel_shader(gaussian_blur_7x7)
compile_pixel_shader(gaussian_blur_9x9)
compile_pixel_shader(gamma_correction_ps)
compile_pixel_shader(imgui_ps)
compile_vertex_shader(imgui_vs)
compile_pixel_shader(resolve_msaa_color_2x)
compile_pixel_shader(resolve_msaa_color_4x)
compile_pixel_shader(resolve_msaa_color_8x)
compile_pixel_shader(resolve_msaa_depth_2x)
compile_pixel_shader(resolve_msaa_depth_4x)
compile_pixel_shader(resolve_msaa_depth_8x)

if (MARATHON_RECOMP_SWITCH)
    # Switch-only variants that gpu/video.cpp picks up with __has_include (tools/generate-switch-app-shaders.sh
    # makes the same headers for the pregenerated build).
    compile_pixel_shader(conditional_survey_store_ps)
    compile_pixel_shader(gamma_correction_push_ps)
    compile_pixel_shader(conditional_survey_slots_ps)
endif()

function(generate_aggregate_header INPUT_DIRECTORY OUTPUT_FILE)
    get_filename_component(ABS_OUTPUT_FILE "${OUTPUT_FILE}" ABSOLUTE)
    file(GLOB_RECURSE HEADER_FILES "${INPUT_DIRECTORY}/*.h")
    set(HEADER_CONTENT "#pragma once\n\n")

    foreach(HEADER_FILE IN LISTS HEADER_FILES)
        get_filename_component(ABS_HEADER_FILE "${HEADER_FILE}" ABSOLUTE)
        if (ABS_HEADER_FILE STREQUAL ABS_OUTPUT_FILE)
            continue()
        endif()
        file(RELATIVE_PATH RELATIVE_HEADER_FILE "${INPUT_DIRECTORY}" "${HEADER_FILE}")
        string(APPEND HEADER_CONTENT "#include \"${RELATIVE_HEADER_FILE}\"\n")
    endforeach()

    if (EXISTS "${OUTPUT_FILE}")
        file(READ "${OUTPUT_FILE}" EXISTING_CONTENT)
        if (EXISTING_CONTENT STREQUAL HEADER_CONTENT)
            return()
        endif()
    endif()

    file(WRITE "${OUTPUT_FILE}" "${HEADER_CONTENT}")
endfunction()

generate_aggregate_header(
    "${CMAKE_CURRENT_SOURCE_DIR}/api"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/Marathon.h"
)

set(RESOURCES_SOURCE_PATH "${PROJECT_SOURCE_DIR}/../MarathonRecompResources")
set(RESOURCES_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/res")

## Miscellaneous ##
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/bc_diff/button_bc_diff.bin" DEST_FILE "${RESOURCES_OUTPUT_PATH}/bc_diff/button_bc_diff.bin" ARRAY_NAME "g_button_bc_diff" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/font/im_font_atlas.bin" DEST_FILE "${RESOURCES_OUTPUT_PATH}/font/im_font_atlas.bin" ARRAY_NAME "g_im_font_atlas" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/font/im_font_atlas.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/font/im_font_atlas.dds" ARRAY_NAME "g_im_font_atlas_texture" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/button_window.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/button_window.dds" ARRAY_NAME "g_button_window" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/controller.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/controller.dds" ARRAY_NAME "g_controller" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/sonicnext-dev.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/sonicnext-dev.dds" ARRAY_NAME "g_sonicnextdev" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/kbm.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/kbm.dds" ARRAY_NAME "g_kbm" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/main_menu1.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/main_menu1.dds" ARRAY_NAME "g_main_menu1" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/main_menu7.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/main_menu7.dds" ARRAY_NAME "g_main_menu7" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/main_menu8.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/main_menu8.dds" ARRAY_NAME "g_main_menu8" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/main_menu9.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/main_menu9.dds" ARRAY_NAME "g_main_menu9" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/arrow.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/arrow.dds" ARRAY_NAME "g_arrow" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/window.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/window.dds" ARRAY_NAME "g_window" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/common/select_arrow.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/common/select_arrow.dds" ARRAY_NAME "g_select_arrow" COMPRESSION_TYPE "zstd")

## Installer ##
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_001.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_001.dds" ARRAY_NAME "g_install_001" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_002.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_002.dds" ARRAY_NAME "g_install_002" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_003.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_003.dds" ARRAY_NAME "g_install_003" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_004.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_004.dds" ARRAY_NAME "g_install_004" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_005.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_005.dds" ARRAY_NAME "g_install_005" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_006.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_006.dds" ARRAY_NAME "g_install_006" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_007.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_007.dds" ARRAY_NAME "g_install_007" COMPRESSION_TYPE "zstd")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/installer/install_008.dds" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/installer/install_008.dds" ARRAY_NAME "g_install_008" COMPRESSION_TYPE "zstd")

## Game Icon ##
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/images/game_icon.bmp" DEST_FILE "${RESOURCES_OUTPUT_PATH}/images/game_icon.bmp" ARRAY_NAME "g_game_icon")

## Audio ##
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/music/installer.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/music/installer.ogg" ARRAY_NAME "g_installer_music")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/window_open.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/window_open.ogg" ARRAY_NAME "g_window_open")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/window_close.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/window_close.ogg" ARRAY_NAME "g_window_close")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/cursor.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/cursor.ogg" ARRAY_NAME "g_cursor")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/deside.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/deside.ogg" ARRAY_NAME "g_deside")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/move.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/move.ogg" ARRAY_NAME "g_move")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/main_deside.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/main_deside.ogg" ARRAY_NAME "g_main_deside")
BIN2C(TARGET_OBJ MarathonRecomp SOURCE_FILE "${RESOURCES_SOURCE_PATH}/sounds/cannot_deside.ogg" DEST_FILE "${RESOURCES_OUTPUT_PATH}/sounds/cannot_deside.ogg" ARRAY_NAME "g_cannot_deside")
