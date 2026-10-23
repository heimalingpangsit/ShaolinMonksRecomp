# Linux build: the same recompiled game and runtime as Windows, with SDL3 for
# the window, input and audio, and Vulkan for rendering. Included from the
# top-level CMakeLists.txt after code generation and runtime adaptation.
find_package(Threads REQUIRED)

set(SHAOLIN_LINUX_EXCLUDE "/kernel/files\\.cpp$|/app/[a-z_]+\\.cpp$|/core/input\\.cpp$|/audio/xaudio2\\.cpp$|/gpu/(d3d12|vertex|shaders|render_thread)\\.cpp$|/xdk/(d3d8|xapi|dsound|xnet)\\.cpp$")
list(FILTER XBE_RUNTIME_SOURCES EXCLUDE REGEX "${SHAOLIN_LINUX_EXCLUDE}")

set(SHAOLIN_IMGUI_CORE
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp")

set(SHAOLIN_SHARED_SOURCES
    src/host.cpp src/keyboard.cpp src/game_info.cpp src/game_files.cpp src/runtime_paths.cpp
    src/game_setup.cpp src/game_import.cpp src/save_storage.cpp src/performance_overlay.cpp
    src/controller.cpp src/launcher.cpp src/launcher_settings.cpp src/game_preferences.cpp
    src/mouse_state.cpp src/raster_compat.cpp
    src/language_pack.cpp src/language_archive.cpp src/language_credits.cpp src/virtual_file.cpp src/mods/runtime.cpp)

# Vulkan: the Khronos headers and glslang's HLSL front end, both pinned. The
# Vulkan loader itself is opened at run time through SDL, so the executable
# does not link against libvulkan.
FetchContent_Declare(vulkan_headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG 2cd90f9d20df57eac214c148f3aed885372ddcfe)  # v1.4.321
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 8a85691a0740d390761a1008b4696f57facd02c4)  # 15.4.0
FetchContent_MakeAvailable(vulkan_headers glslang)

set(SHAOLIN_LINUX_SOURCES
    src/linux/linux_main.cpp src/linux/linux_platform.cpp src/linux/linux_input.cpp
    src/linux/linux_audio.cpp src/linux/linux_launcher_window.cpp src/linux/gpu_select.cpp src/linux/gpu_null.cpp
    src/linux/vk/vk_api.cpp src/linux/vk/vk_shader.cpp src/linux/vk/vk_backend.cpp src/linux/vk/bc_decode.cpp
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")

add_executable(ShaolinMonks ${XBE_RUNTIME_SOURCES} ${SHAOLIN_IMGUI_CORE} ${GEN_SOURCES}
    "${HOST_ADAPTED}/boot.cpp" "${HOST_ADAPTED}/d3d8.cpp" "${HOST_ADAPTED}/xapi.cpp" "${HOST_ADAPTED}/dsound.cpp"
    "${HOST_ADAPTED}/vertex.cpp" "${HOST_ADAPTED}/shaders.cpp" "${HOST_ADAPTED}/files.cpp" "${HOST_ADAPTED}/settings.cpp"
    "${HOST_ADAPTED}/render_thread.cpp"
    ${SHAOLIN_SHARED_SOURCES} ${SHAOLIN_LINUX_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/shaolin_xbe.cpp")
add_dependencies(ShaolinMonks shaolin_codegen)
set_target_properties(ShaolinMonks PROPERTIES OUTPUT_NAME ShaolinMonksRecomp
    BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
target_include_directories(ShaolinMonks PRIVATE src src/linux/compat resources "${SHAOLIN_RECOMP_SDK}/runtime" "${GEN}"
    "${sdl3_SOURCE_DIR}/include")
target_include_directories(ShaolinMonks SYSTEM PRIVATE "${imgui_SOURCE_DIR}")
target_compile_definitions(ShaolinMonks PRIVATE _CRT_SECURE_NO_WARNINGS VK_NO_PROTOTYPES IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
set_source_files_properties(src/linux/linux_main.cpp PROPERTIES COMPILE_DEFINITIONS "SHAOLIN_RECOMP_VERSION=\"${PROJECT_VERSION}\"")
target_compile_options(ShaolinMonks PRIVATE -fno-strict-aliasing -march=x86-64-v2)
# libstdc++ and libgcc go into the executable: the player's distribution only has to supply glibc
target_link_options(ShaolinMonks PRIVATE -fuse-ld=lld -static-libstdc++ -static-libgcc)
target_link_libraries(ShaolinMonks PRIVATE shaolin_mods miniz tomlplusplus::tomlplusplus SDL3::SDL3-static Threads::Threads
    Vulkan::Headers glslang::glslang glslang::glslang-default-resource-limits ${CMAKE_DL_LIBS})

# Regression tests that do not depend on Windows.
enable_testing()
function(shaolin_linux_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name}_test ${T_SOURCES})
    target_include_directories(${name}_test PRIVATE src src/linux/compat "${SHAOLIN_RECOMP_SDK}/runtime" "${HOST_ADAPTED}")
    target_compile_definitions(${name}_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(${name}_test PRIVATE -fuse-ld=lld)
    if(T_LIBS)
        target_link_libraries(${name}_test PRIVATE ${T_LIBS})
    endif()
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()
shaolin_linux_test(recomp_virtual_file SOURCES tests/recomp_virtual_file_test.cpp src/virtual_file.cpp)
shaolin_linux_test(recomp_language_archive SOURCES tests/recomp_language_archive_test.cpp src/language_archive.cpp src/virtual_file.cpp LIBS miniz)
shaolin_linux_test(recomp_language_credits SOURCES tests/recomp_language_credits_test.cpp src/language_credits.cpp)
shaolin_linux_test(recomp_language_pack SOURCES tests/recomp_language_pack_test.cpp src/language_pack.cpp src/language_archive.cpp
    src/virtual_file.cpp "${SHAOLIN_RECOMP_SDK}/runtime/kernel/crypto.cpp" LIBS miniz shaolin_mods)
shaolin_linux_test(recomp_launcher_settings SOURCES tests/recomp_launcher_settings_test.cpp src/launcher_settings.cpp LIBS tomlplusplus::tomlplusplus)
shaolin_linux_test(recomp_mouse SOURCES tests/recomp_mouse_test.cpp src/mouse_state.cpp)
shaolin_linux_test(recomp_sfd_lock SOURCES tests/recomp_sfd_lock_test.cpp)
shaolin_linux_test(recomp_vblank SOURCES tests/recomp_vblank_test.cpp)
shaolin_linux_test(recomp_input_mode SOURCES tests/recomp_input_mode_test.cpp)
add_test(NAME language_pack_preparation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/language_pack_test.py")
add_test(NAME recomp_package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/recomp_package_test.py")

install(TARGETS ShaolinMonks RUNTIME DESTINATION .)
install(FILES resources/readme.txt DESTINATION .)
install(FILES LICENSE DESTINATION licenses RENAME LICENSE.txt)
install(DIRECTORY licenses/ DESTINATION licenses)
