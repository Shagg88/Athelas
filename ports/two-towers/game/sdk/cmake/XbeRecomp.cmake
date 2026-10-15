# Original Xbox static recompilation: xbe_recomp, the host runtime and add_xbe_image()
include_guard(GLOBAL)

get_filename_component(XBE_SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
# build scripts find the SDK's xdk folder and THIRD_PARTY.txt through this
file(TO_NATIVE_PATH "${XBE_SDK_DIR}" XBE_SDK_DIR_NATIVE)
file(WRITE ${CMAKE_BINARY_DIR}/sdk_dir.txt "${XBE_SDK_DIR_NATIVE}\n")

# the C and C++ runtimes built into every program (/MT): the exe runs without the Visual C++ Redistributable. Set
# before the libraries below so they all match, and for those that ask an older CMake as well (CMP0091)
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

include(FetchContent)
# the executables' manifest (runtime/app/app.manifest) goes in as a resource
if(WIN32)
    enable_language(RC)
    # the linker must not add its own manifest (also resource 1 of type 24): clang's link line has /MANIFEST:EMBED
    # built in, which lld-link turns into "duplicate resource" against ours
    foreach(lang C CXX)
        string(REPLACE "/MANIFEST:EMBED" "/MANIFEST:NO" CMAKE_${lang}_LINK_EXECUTABLE "${CMAKE_${lang}_LINK_EXECUTABLE}")
    endforeach()
    if(MSVC AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        add_link_options(/MANIFEST:NO)
    endif()
endif()
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.0 GIT_SHALLOW TRUE)
# SDL3 for controllers, static so it links into the exe
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.18 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus imgui SDL3)

# x86 to C++ recompiler
file(GLOB XBE_RECOMP_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/tools/recomp/*.cpp)
add_executable(xbe_recomp ${XBE_RECOMP_SOURCES})
target_include_directories(xbe_recomp PRIVATE ${XBE_SDK_DIR}/runtime ${XBE_SDK_DIR}/tools/recomp)
target_compile_definitions(xbe_recomp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)

# XDK signature scanner that writes a new title's config
add_executable(xbe_symbols ${XBE_SDK_DIR}/tools/symbols/main.cpp)
target_include_directories(xbe_symbols PRIVATE ${XBE_SDK_DIR}/runtime)
target_compile_definitions(xbe_symbols PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_symbols PRIVATE Zydis tomlplusplus::tomlplusplus)

set(XBE_IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)

file(GLOB XBE_RUNTIME_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/runtime/*/*.cpp)
# SDL's headers go to the input code alone (it is linked LINK_ONLY), so the generated sources' compile line stays the same
set_source_files_properties(${XBE_SDK_DIR}/runtime/core/input.cpp PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")

# One executable per recompiled image: its code from config/<config> (partitions as that config sets them),
# the SDK runtime, XBE_HOST_SOURCES shared by every image of the game, and src/<game_dir> hooks only this image has.
# Expects assets/<xbe> and the config's out = "generated/<game_dir>". At run time the game may be a disc image instead.
function(add_xbe_image target xbe config partitions game_dir)
    set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_SDK_DIR}/xdk/*/*.toml)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h
        COMMAND xbe_recomp config/${config} --xdk ${XBE_SDK_DIR}/xdk
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${configs} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE
        COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-Wno-unused-label;-Wno-parentheses-equality;-Wno-unused-variable")
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the executable checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    file(SHA1 ${xbe_file} xbe_sha1)
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    # UTF-8 paths, DPI awareness: runtime/app/app.manifest, as resource 1 of type 24 (RT_MANIFEST)
    set(XBE_RC ${CMAKE_CURRENT_BINARY_DIR}/${target}_manifest.rc)
    set(rc "1 24 \"${XBE_SDK_DIR}/runtime/app/app.manifest\"\n")
    # XBE_VERSION ("1.2.3") and XBE_PRODUCT, when the game sets them: the version Windows shows in the exe's
    # Properties, and GAME_VERSION for the code
    if(XBE_VERSION)
        string(REPLACE "." "," v "${XBE_VERSION}")
        string(APPEND rc "1 VERSIONINFO\nFILEVERSION ${v},0\nPRODUCTVERSION ${v},0\nFILEOS 0x40004\nFILETYPE 0x1\nBEGIN\n"
            "BLOCK \"StringFileInfo\"\nBEGIN\nBLOCK \"040904b0\"\nBEGIN\n"
            "VALUE \"FileDescription\", \"${XBE_PRODUCT}\"\nVALUE \"ProductName\", \"${XBE_PRODUCT}\"\n"
            "VALUE \"FileVersion\", \"${XBE_VERSION}\"\nVALUE \"ProductVersion\", \"${XBE_VERSION}\"\n"
            "VALUE \"InternalName\", \"${target}\"\nVALUE \"OriginalFilename\", \"${target}.exe\"\nEND\nEND\n"
            "BLOCK \"VarFileInfo\"\nBEGIN\nVALUE \"Translation\", 0x409, 1200\nEND\nEND\n")
    endif()
    file(CONFIGURE OUTPUT ${XBE_RC} CONTENT "${rc}")
    set_property(SOURCE ${XBE_RC} APPEND PROPERTY OBJECT_DEPENDS ${XBE_SDK_DIR}/runtime/app/app.manifest)
    add_executable(${target} WIN32 ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES} ${XBE_IMGUI_SOURCES} ${XBE_HASH} ${XBE_RC})
    add_dependencies(${target} recompile_${target})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    if(XBE_VERSION)
        target_compile_definitions(${target} PRIVATE GAME_VERSION="${XBE_VERSION}")
    endif()
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> d3d12 dxgi dxguid d3dcompiler xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    # lld keeps the recompiled weak functions overridable by the host ones
    target_link_options(${target} PRIVATE -fuse-ld=lld -g)
endfunction()
