project("MarathonRecompLib")

set(MARATHON_RECOMP_SWITCH_PREGENERATED_SHADER_CACHE_DEFAULT OFF)
if (MARATHON_RECOMP_SWITCH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shader/shader_cache.cpp")
    set(MARATHON_RECOMP_SWITCH_PREGENERATED_SHADER_CACHE_DEFAULT ON)
endif()

option(
    MARATHON_RECOMP_SWITCH_USE_PREGENERATED_SHADERS
    "Use an existing shader/shader_cache.cpp during Switch builds instead of running XenosRecomp from the target build."
    ${MARATHON_RECOMP_SWITCH_PREGENERATED_SHADER_CACHE_DEFAULT}
)

set(MARATHON_RECOMP_SWITCH_PREGENERATED_PPC_DEFAULT OFF)
if (MARATHON_RECOMP_SWITCH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_config.h")
    set(MARATHON_RECOMP_SWITCH_PREGENERATED_PPC_DEFAULT ON)
endif()

option(
    MARATHON_RECOMP_SWITCH_USE_PREGENERATED_PPC
    "Use existing ppc/ sources during Switch builds instead of running XenonRecomp from the target build."
    ${MARATHON_RECOMP_SWITCH_PREGENERATED_PPC_DEFAULT}
)

add_compile_options(
    -fno-strict-aliasing
)

if (WIN32)
    add_compile_options(/fp:strict)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ffp-model=strict>)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-frounding-math>)
endif()

if (MARATHON_RECOMP_SWITCH AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # The guest never sees the host's errno or floating-point exception flags (mffs returns only the
    # rounding mode), so the recompiled code does not need to preserve them: -fno-math-errno turns sqrt()
    # into a single fsqrt instead of a check plus a possible libm call per square root, and
    # -fno-trapping-math lets GCC if-convert floating-point code (FP traps are disabled on the host anyway).
    # Neither changes any value: rounding is still honoured (-frounding-math), and no fast-math
    # reassociation is enabled. Under GCC's default contraction they could in principle change which
    # separate multiplies and adds get fused; checked on all of ppc/ (fused instructions per function, with
    # and without each flag): -fno-math-errno changes none, and the only difference -fno-trapping-math
    # makes is a block it duplicates or merges, each copy with the same fused operation.
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-fno-math-errno>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-trapping-math>
    )

    # AArch64 GCC keeps a frame pointer by default: every non-leaf recompiled function set up x29 and
    # could not use it as a register. The guest registers live in the context, so the recompiled code is
    # short of registers; it gets x29 back. Only here: the port's own code keeps frame pointers for crash
    # backtraces (os/switch/exception_switch.cpp reloads x29 from the exception dump, it walks no chain).
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fomit-frame-pointer>)

    # os/switch/exception_switch.cpp resumes an emulated guest access through x16 without restoring it, so no
    # value may live in x16 across a guest load or store. Register allocation only.
    if (MARATHON_RECOMP_SWITCH_FIXED_X16)
        add_compile_options(-ffixed-x16)
    endif()

    # Opt-in exact FMA: the guest's own fused operations are explicit fma (PPC_EXPLICIT_FMA, root CMakeLists.txt);
    # nothing else fuses. _ACTIVE: requested, not classic, and ppc/ has the explicit fma text.
    if (MARATHON_RECOMP_SWITCH_EXACT_FMA_ACTIVE)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)
    endif()

    # Guest memory accesses of the recompiled code as plain instead of volatile accesses (ppc_context.h): the compiler
    # may forward, pair and schedule them, while the loop barriers on backward branches keep wait loops re-reading
    # memory and the guest's sync/lwsync/eieio stay fences. The app keeps volatile accesses. Not with the classic
    # code generation, which has no loop barriers.
    if (MARATHON_RECOMP_SWITCH_PLAIN_GUEST_MEMORY AND NOT MARATHON_RECOMP_SWITCH_CLASSIC_CODEGEN)
        add_compile_definitions(PPC_PLAIN_GUEST_MEMORY)
    endif()
endif()

if (MARATHON_RECOMP_SWITCH)
    set(MARATHON_RECOMP_HOST_TOOLS_DIR "${CMAKE_SOURCE_DIR}/build/host-tools" CACHE PATH "Directory containing host-side generation tools.")
    set(MARATHON_RECOMP_HOST_U8EXTRACT "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/u8extract/u8extract${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host u8extract executable.")
    set(MARATHON_RECOMP_HOST_XENON_RECOMP "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/XenonRecomp/XenonRecomp/XenonRecomp${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host XenonRecomp executable.")
    set(MARATHON_RECOMP_HOST_XENOS_RECOMP "${MARATHON_RECOMP_HOST_TOOLS_DIR}/tools/XenosRecomp/XenosRecomp/XenosRecomp${CMAKE_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Host XenosRecomp executable.")
    set(MARATHON_RECOMP_XENON_RECOMP_COMMAND
        "${MARATHON_RECOMP_HOST_XENON_RECOMP}"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/Marathon.toml"
        "${MARATHON_RECOMP_TOOLS_ROOT}/XenonRecomp/XenonUtils/ppc_context.h"
    )
    set(MARATHON_RECOMP_XENON_RECOMP_DEPENDS "${MARATHON_RECOMP_HOST_XENON_RECOMP}")
    set(MARATHON_RECOMP_U8EXTRACT_COMMAND "${MARATHON_RECOMP_HOST_U8EXTRACT}")
    set(MARATHON_RECOMP_U8EXTRACT_DEPENDS "${MARATHON_RECOMP_HOST_U8EXTRACT}")
    set(MARATHON_RECOMP_XENOS_RECOMP_COMMAND
        "${MARATHON_RECOMP_HOST_XENOS_RECOMP}"
        "${CMAKE_CURRENT_SOURCE_DIR}/private/shader"
        "${CMAKE_CURRENT_SOURCE_DIR}/shader/shader_cache.cpp"
        "${MARATHON_RECOMP_TOOLS_ROOT}/XenosRecomp/XenosRecomp/shader_common.h"
    )
    set(MARATHON_RECOMP_XENOS_RECOMP_DEPENDS "${MARATHON_RECOMP_HOST_XENOS_RECOMP}")
else()
    target_compile_definitions(XenonRecomp PRIVATE
        XENON_RECOMP_CONFIG_FILE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/config/Marathon.toml\"
        XENON_RECOMP_HEADER_FILE_PATH=\"${MARATHON_RECOMP_TOOLS_ROOT}/XenonRecomp/XenonUtils/ppc_context.h\")
    set(MARATHON_RECOMP_XENON_RECOMP_COMMAND "$<TARGET_FILE:XenonRecomp>")
    set(MARATHON_RECOMP_U8EXTRACT_COMMAND "$<TARGET_FILE:u8extract>")
    set(MARATHON_RECOMP_XENOS_RECOMP_COMMAND "$<TARGET_FILE:XenosRecomp>")
endif()

set(MARATHON_RECOMP_PPC_RECOMPILED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_context.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_func_mapping.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_recomp_shared.h"
)

set(MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT 0)
if (MARATHON_RECOMP_SWITCH AND MARATHON_RECOMP_SWITCH_USE_PREGENERATED_PPC)
    # tools/build-switch.sh generates ppc/ before it configures: take as many files as XenonRecomp wrote
    # (256 functions each, so the count follows the functions of Marathon.toml), in the usual order.
    while (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_recomp.${MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT}.cpp")
        math(EXPR MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT "${MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT} + 1")
    endwhile()
endif()
if (MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT EQUAL 0)
    set(MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT 146)
endif()
math(EXPR MARATHON_RECOMP_PPC_RECOMP_LAST_FILE "${MARATHON_RECOMP_PPC_RECOMP_FILE_COUNT} - 1")

foreach(i RANGE 0 ${MARATHON_RECOMP_PPC_RECOMP_LAST_FILE})
    list(APPEND MARATHON_RECOMP_PPC_RECOMPILED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_recomp.${i}.cpp")
endforeach()

if (MARATHON_RECOMP_SWITCH AND MARATHON_RECOMP_SWITCH_USE_PREGENERATED_PPC)
    message(STATUS "Using pre-generated PPC sources: ${CMAKE_CURRENT_SOURCE_DIR}/ppc")
else()
    add_custom_command(
        OUTPUT
            ${MARATHON_RECOMP_PPC_RECOMPILED_SOURCES}
        COMMAND
            ${MARATHON_RECOMP_XENON_RECOMP_COMMAND}
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/private/default.xex"
            "${CMAKE_CURRENT_SOURCE_DIR}/config/Marathon.toml"
            ${MARATHON_RECOMP_XENON_RECOMP_DEPENDS}
        USES_TERMINAL
    )
endif()

set(XENOS_RECOMP_ROOT "${MARATHON_RECOMP_TOOLS_ROOT}/XenosRecomp/XenosRecomp")
set(XENOS_RECOMP_INCLUDE "${XENOS_RECOMP_ROOT}/shader_common.h")

file(GLOB XENOS_RECOMP_SOURCES
    "${XENOS_RECOMP_ROOT}/*.h"
    "${XENOS_RECOMP_ROOT}/*.cpp"
)

if (MARATHON_RECOMP_SWITCH AND MARATHON_RECOMP_SWITCH_USE_PREGENERATED_SHADERS)
    message(STATUS "Using pre-generated Switch shader cache: ${CMAKE_CURRENT_SOURCE_DIR}/shader/shader_cache.cpp")
else()
    add_custom_command(
        OUTPUT
            "${CMAKE_CURRENT_SOURCE_DIR}/private/shader"
        COMMAND
            ${MARATHON_RECOMP_U8EXTRACT_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/private/shader.arc" "${CMAKE_CURRENT_SOURCE_DIR}/private/shader"
        COMMAND
            ${MARATHON_RECOMP_U8EXTRACT_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/private/shader_lt.arc" "${CMAKE_CURRENT_SOURCE_DIR}/private/shader"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/private/shader.arc"
            "${CMAKE_CURRENT_SOURCE_DIR}/private/shader_lt.arc"
            ${MARATHON_RECOMP_U8EXTRACT_DEPENDS}
    )

    if (NOT MARATHON_RECOMP_SWITCH)
        target_compile_definitions(XenosRecomp PRIVATE
            XENOS_RECOMP_INPUT=\"${CMAKE_CURRENT_SOURCE_DIR}/private/shader\"
            XENOS_RECOMP_OUTPUT=\"${CMAKE_CURRENT_SOURCE_DIR}/shader/shader_cache.cpp\"
            XENOS_RECOMP_INCLUDE_INPUT=\"${XENOS_RECOMP_INCLUDE}\"
            MARATHON_RECOMP
        )
    endif()

    add_custom_command(
        OUTPUT
            "${CMAKE_CURRENT_SOURCE_DIR}/shader/shader_cache.cpp"
        COMMAND
            ${MARATHON_RECOMP_XENOS_RECOMP_COMMAND}
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/private/shader"
            ${XENOS_RECOMP_SOURCES}
            ${XENOS_RECOMP_INCLUDE}
            ${MARATHON_RECOMP_XENOS_RECOMP_DEPENDS}
        USES_TERMINAL
    )
endif()

add_library(MarathonRecompLib
    ${MARATHON_RECOMP_PPC_RECOMPILED_SOURCES}
    "shader/shader_cache.h"
    "shader/shader_cache.cpp"
)

target_include_directories(MarathonRecompLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(MarathonRecompLib PRIVATE "${MARATHON_RECOMP_TOOLS_ROOT}/XenonRecomp/thirdparty/simde")
target_precompile_headers(MarathonRecompLib PUBLIC "ppc/ppc_recomp_shared.h")

if (MARATHON_RECOMP_SWITCH AND MARATHON_RECOMP_SWITCH_RECOMP_O2)
    message(STATUS "Switch: -O2 for the recompiled code")
    target_compile_options(MarathonRecompLib PRIVATE -O2)
endif()
