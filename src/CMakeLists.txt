add_library(obgi_core INTERFACE)
# keep fetched SDL3 quiet: its own warnings are not ours
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
target_include_directories(obgi_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(obgi_core INTERFACE cxx_std_23)
if(ANDROID)
  # NDK r27 libc++ gates std::jthread behind -fexperimental-library.
  target_compile_options(obgi_core INTERFACE -fexperimental-library)
endif()

add_library(obgi_formats STATIC
  formats/dsc.cpp formats/cbg.cpp formats/bm.cpp formats/bw.cpp formats/sdc.cpp formats/gdb.cpp)
target_link_libraries(obgi_formats PUBLIC obgi_core)
obgi_target_defaults(obgi_formats)

add_library(obgi_vm STATIC vm/paged_segment.cpp vm/ctx.cpp vm/interp.cpp vm/scheduler.cpp vm/core_subops.cpp)
target_link_libraries(obgi_vm PUBLIC obgi_core)
obgi_target_defaults(obgi_vm)

add_executable(obgi_bench_memory EXCLUDE_FROM_ALL ../tools/bench_memory/main.cpp)
target_link_libraries(obgi_bench_memory PRIVATE obgi_vm)
obgi_target_defaults(obgi_bench_memory)

add_library(obgi_vfs STATIC vfs/mapped_file.cpp vfs/arc.cpp)
target_link_libraries(obgi_vfs PUBLIC obgi_core)
obgi_target_defaults(obgi_vfs)

add_library(obgi_resources STATIC resources/archive_store.cpp)
target_link_libraries(obgi_resources PUBLIC obgi_vfs obgi_formats)
obgi_target_defaults(obgi_resources)

add_library(obgi_programs STATIC bindings/arc_program_provider.cpp)
target_link_libraries(obgi_programs PUBLIC obgi_resources obgi_vm)
obgi_target_defaults(obgi_programs)

if(APPLE AND NOT IOS)
  # Lightweight folder-inspection library for the macOS launcher (no SDL, no FFmpeg).
  add_library(obgi_probe SHARED api/probe.cpp)
  target_include_directories(obgi_probe PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(obgi_probe PRIVATE obgi_programs)
  target_compile_definitions(obgi_probe PRIVATE OPENBGI_VERSION="${PROJECT_VERSION}")
  set_target_properties(obgi_probe PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden INSTALL_NAME_DIR "@rpath")
  obgi_target_defaults(obgi_probe)
endif()

if(NOT OPENBGI_BUILD_RUNTIME)
  return()
endif()

obgi_fetch_sdl3()
if(NOT APPLE)
  obgi_fetch_glslang()
endif()
obgi_fetch_dxc()
obgi_fetch_freetype()
obgi_fetch_ffmpeg()

if(CMAKE_CROSSCOMPILING)
  find_program(OBGI_HOST_CXX NAMES clang++ g++ c++
    PATHS /usr/bin /usr/local/bin /opt/homebrew/bin
    NO_DEFAULT_PATH REQUIRED)
  set(OBGI_SHADERPACK_BIN "${CMAKE_CURRENT_BINARY_DIR}/obgi_shaderpack_host" CACHE INTERNAL "")
  add_custom_command(
    OUTPUT "${OBGI_SHADERPACK_BIN}"
    COMMAND ${CMAKE_COMMAND} -E env --unset=SDKROOT --unset=PLATFORM_NAME --unset=IPHONEOS_DEPLOYMENT_TARGET
            "${OBGI_HOST_CXX}" -std=c++23 -O2 "-I${CMAKE_CURRENT_SOURCE_DIR}"
            "${PROJECT_SOURCE_DIR}/tools/shaderpack/main.cpp" -o "${OBGI_SHADERPACK_BIN}"
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/shaderpack/main.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/render/shader_pack.hpp"
    COMMENT "Building host obgi_shaderpack" VERBATIM)
  add_custom_target(obgi_shaderpack DEPENDS "${OBGI_SHADERPACK_BIN}")
else()
  add_executable(obgi_shaderpack ../tools/shaderpack/main.cpp)
  target_include_directories(obgi_shaderpack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  obgi_target_defaults(obgi_shaderpack)
endif()

obgi_shader_pack(OBGI_SHADER_PACK NAME obgi SHADERS blend_cases.comp blit.vert blit.frag blit_rect.comp crossfade.comp screen_filter.comp slot_present.comp
  DEPENDS "${PROJECT_SOURCE_DIR}/shaders/blend.glsl")
set(OBGI_SHADER_PACK "${OBGI_SHADER_PACK}" CACHE INTERNAL "")
add_library(obgi_render STATIC render/gpu_blend.cpp render/display.cpp render/background_fx.cpp render/vector_maps.cpp render/renderer.cpp render/shader_pack.cpp render/shader_library.cpp)
add_dependencies(obgi_render obgi_shaders)
# development fallback when the pack is not next to the executable
target_compile_definitions(obgi_render PRIVATE OBGI_BUILD_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
target_link_libraries(obgi_render PUBLIC obgi_core PRIVATE obgi_vfs SDL3::SDL3)
obgi_target_defaults(obgi_render)

add_library(obgi_scene STATIC services/scene.cpp)
target_link_libraries(obgi_scene PUBLIC obgi_core)
obgi_target_defaults(obgi_scene)
target_link_libraries(obgi_render PUBLIC obgi_scene)

add_library(obgi_text STATIC text/encoding.cpp text/font.cpp text/layout.cpp)
target_link_libraries(obgi_text PUBLIC obgi_core PRIVATE freetype obgi_vfs)
if(APPLE)
  target_link_libraries(obgi_text PRIVATE "-framework CoreText" "-framework CoreFoundation")
endif()
obgi_target_defaults(obgi_text)

add_library(obgi_audio STATIC audio/av_input.cpp audio/pcm.cpp audio/stream.cpp audio/mixer.cpp audio/device.cpp)
target_link_libraries(obgi_audio PUBLIC obgi_core PRIVATE obgi_formats FFmpeg::avformat FFmpeg::avcodec FFmpeg::swresample FFmpeg::avutil SDL3::SDL3)
obgi_target_defaults(obgi_audio)

add_library(obgi_video STATIC video/movie.cpp video/player.cpp video/session_player.cpp)
target_link_libraries(obgi_video PUBLIC obgi_core obgi_render obgi_audio PRIVATE FFmpeg::swscale FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil)
obgi_target_defaults(obgi_video)

add_library(obgi_bindings STATIC bindings/arc_image_source.cpp bindings/graphics_subops.cpp bindings/text_subops.cpp bindings/audio_subops.cpp bindings/movie_subops.cpp bindings/system_subops.cpp bindings/font_subops.cpp bindings/window_subops.cpp bindings/table_subops.cpp bindings/plane_subops.cpp bindings/panel.cpp bindings/message_subops.cpp bindings/knob_subops.cpp bindings/sequence_subops.cpp bindings/bitmap_effect_subops.cpp bindings/particle_subops.cpp)
target_link_libraries(obgi_bindings PUBLIC obgi_resources obgi_programs obgi_vm obgi_vfs obgi_formats obgi_render obgi_text obgi_audio obgi_video)
obgi_target_defaults(obgi_bindings)

add_library(obgi_services STATIC services/input.cpp services/message.cpp services/sequence.cpp)
target_link_libraries(obgi_services PUBLIC obgi_core obgi_render obgi_formats)
obgi_target_defaults(obgi_services)
target_link_libraries(obgi_bindings PUBLIC obgi_services)

if(NOT IOS AND NOT ANDROID)
  add_executable(obgi_view ../tools/view/main.cpp)
  target_link_libraries(obgi_view PRIVATE obgi_formats obgi_vfs obgi_render obgi_audio obgi_video)
  obgi_target_defaults(obgi_view)
  obgi_install_shaders(obgi_view obgi_shaders "${OBGI_SHADER_PACK}")
endif()

add_library(obgi_runtime STATIC runtime/session.cpp)
target_link_libraries(obgi_runtime PUBLIC obgi_bindings PRIVATE SDL3::SDL3)
obgi_target_defaults(obgi_runtime)

add_library(obgi_app STATIC api/openbgi.cpp api/probe.cpp)
target_include_directories(obgi_app PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(obgi_app PUBLIC obgi_runtime PRIVATE SDL3::SDL3)
target_compile_definitions(obgi_app PRIVATE OPENBGI_VERSION="${PROJECT_VERSION}")
set_target_properties(obgi_app PROPERTIES POSITION_INDEPENDENT_CODE ON)
obgi_target_defaults(obgi_app)

if(ANDROID)
  add_library(main SHARED api/android_main.cpp api/jni_android.cpp)
  target_link_libraries(main PRIVATE obgi_app SDL3::SDL3 android log)
  target_link_options(main PRIVATE -Wl,-z,max-page-size=16384)
  add_dependencies(main obgi_shaders)
  obgi_target_defaults(main)
elseif(NOT IOS)
  add_executable(obgi_run ../tools/run/main.cpp)
  target_link_libraries(obgi_run PRIVATE obgi_app)
  obgi_target_defaults(obgi_run)
  obgi_install_shaders(obgi_run obgi_shaders "${OBGI_SHADER_PACK}")

  add_executable(obgi_loop ../tools/loop/main.cpp)
  target_link_libraries(obgi_loop PRIVATE obgi_app)
  obgi_target_defaults(obgi_loop)
  obgi_install_shaders(obgi_loop obgi_shaders "${OBGI_SHADER_PACK}")
endif()

