add_library(obgi_core INTERFACE)
# keep fetched SDL3 quiet: its own warnings are not ours
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
target_include_directories(obgi_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(obgi_core INTERFACE cxx_std_23)

add_library(obgi_formats STATIC
  formats/dsc.cpp formats/cbg.cpp formats/bm.cpp formats/bw.cpp formats/sdc.cpp formats/gdb.cpp)
target_link_libraries(obgi_formats PUBLIC obgi_core)
obgi_target_defaults(obgi_formats)

obgi_fetch_sdl3()
obgi_fetch_glslang()
obgi_fetch_spirv_cross()
obgi_fetch_dxc()
obgi_fetch_freetype()
obgi_fetch_ffmpeg()

add_executable(obgi_shaderpack ../tools/shaderpack/main.cpp)
target_include_directories(obgi_shaderpack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
obgi_target_defaults(obgi_shaderpack)

add_executable(obgi_spvmsl ../tools/spvmsl/main.cpp)
target_include_directories(obgi_spvmsl PRIVATE "${spirv_cross_SOURCE_DIR}")
target_link_libraries(obgi_spvmsl PRIVATE spirv-cross-msl spirv-cross-core spirv-cross-glsl)

obgi_shader_pack(OBGI_SHADER_PACK NAME obgi SHADERS blend_cases.comp blit.vert blit.frag blit_rect.comp slot_present.comp
  DEPENDS "${PROJECT_SOURCE_DIR}/shaders/blend.glsl")
set(OBGI_SHADER_PACK "${OBGI_SHADER_PACK}" CACHE INTERNAL "")
add_library(obgi_render STATIC render/gpu_blend.cpp render/display.cpp render/renderer.cpp render/shader_pack.cpp render/shader_library.cpp)
# development fallback when the pack is not next to the executable
target_compile_definitions(obgi_render PRIVATE OBGI_BUILD_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
target_link_libraries(obgi_render PUBLIC obgi_core PRIVATE obgi_vfs SDL3::SDL3)
obgi_target_defaults(obgi_render)

add_library(obgi_text STATIC text/encoding.cpp text/font.cpp text/layout.cpp)
target_link_libraries(obgi_text PUBLIC obgi_core PRIVATE freetype)
obgi_target_defaults(obgi_text)

add_library(obgi_audio STATIC audio/av_input.cpp audio/pcm.cpp audio/mixer.cpp audio/device.cpp)
target_link_libraries(obgi_audio PUBLIC obgi_core PRIVATE obgi_formats FFmpeg::avformat FFmpeg::avcodec FFmpeg::swresample FFmpeg::avutil SDL3::SDL3)
obgi_target_defaults(obgi_audio)

add_library(obgi_video STATIC video/movie.cpp video/player.cpp)
target_link_libraries(obgi_video PUBLIC obgi_core obgi_render obgi_audio PRIVATE FFmpeg::swscale FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil)
obgi_target_defaults(obgi_video)

add_library(obgi_vm STATIC vm/ctx.cpp vm/interp.cpp vm/scheduler.cpp vm/core_subops.cpp)
target_link_libraries(obgi_vm PUBLIC obgi_core)
obgi_target_defaults(obgi_vm)

add_library(obgi_bindings STATIC bindings/arc_program_provider.cpp bindings/arc_resource_finder.cpp bindings/arc_image_source.cpp bindings/graphics_subops.cpp bindings/text_subops.cpp bindings/audio_subops.cpp bindings/movie_subops.cpp bindings/system_subops.cpp bindings/font_subops.cpp bindings/window_subops.cpp bindings/table_subops.cpp bindings/plane_subops.cpp bindings/panel.cpp bindings/message_subops.cpp bindings/knob_subops.cpp bindings/sequence_subops.cpp bindings/bitmap_effect_subops.cpp bindings/particle_subops.cpp)
target_link_libraries(obgi_bindings PUBLIC obgi_vm obgi_vfs obgi_formats obgi_render obgi_text obgi_audio obgi_video)
obgi_target_defaults(obgi_bindings)

add_library(obgi_vfs STATIC vfs/mapped_file.cpp vfs/arc.cpp)
target_link_libraries(obgi_vfs PUBLIC obgi_core)
obgi_target_defaults(obgi_vfs)

add_executable(obgi_view ../tools/view/main.cpp)
target_link_libraries(obgi_view PRIVATE obgi_formats obgi_vfs obgi_render obgi_audio obgi_video)
obgi_target_defaults(obgi_view)
obgi_install_shaders(obgi_view obgi_shaders "${OBGI_SHADER_PACK}")

add_executable(obgi_run ../tools/run/main.cpp)
target_link_libraries(obgi_run PRIVATE obgi_bindings)
obgi_target_defaults(obgi_run)
obgi_install_shaders(obgi_run obgi_shaders "${OBGI_SHADER_PACK}")
