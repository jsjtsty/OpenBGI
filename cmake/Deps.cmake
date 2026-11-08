# Third-party dependencies, all fetched by CMake (downloads honour OPENBGI_PROXY, see Proxy.cmake).
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # some dependencies declare ancient cmake_minimum_required

function(obgi_fetch_sdl3)
  if(TARGET SDL3::SDL3)
    return()
  endif()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE) # linked statically: one self-contained executable
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.20
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
endfunction()

# glslang provides the GLSL -> SPIR-V compiler used at build time.
function(obgi_fetch_glslang)
  if(TARGET glslang-standalone)
    return()
  endif()
  set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
  set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
  set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
  set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
  set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
  set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 16.6.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glslang)
endfunction()

# SPIR-V -> HLSL / MSL translation (the same component SDL_shadercross wraps; its vendored DXC build would
# compile all of LLVM, so the pieces are used directly: SPIRV-Cross here, a prebuilt DXC below).
function(obgi_fetch_spirv_cross)
  if(TARGET spirv-cross)
    return()
  endif()
  set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_Declare(spirv_cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(spirv_cross)
endfunction()

# D3D12 needs DXIL, which only Microsoft's DXC produces. Prebuilt binaries are downloaded on Windows hosts.
function(obgi_fetch_dxc)
  if(NOT WIN32 OR OBGI_DXC)
    return()
  endif()
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
    set(arch arm64)
  else()
    set(arch x64)
  endif()
  FetchContent_Declare(dxc
    URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(dxc)
  set(OBGI_DXC "${dxc_SOURCE_DIR}/bin/${arch}/dxc.exe" CACHE FILEPATH "dxc compiler used for DXIL output" FORCE)
endfunction()

# obgi_install_shaders(<exe-target> <pack-target> <pack-file>): copies the pack to <exe dir>/shaders/ after each build.
function(obgi_install_shaders exe pack_target pack_file)
  add_dependencies(${exe} ${pack_target})
  add_custom_command(TARGET ${exe} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${exe}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${pack_file}" "$<TARGET_FILE_DIR:${exe}>/shaders/"
    VERBATIM)
endfunction()

# obgi_shader_pack(<out-pack-var> NAME <pack> SHADERS <glsl files in shaders/>... [DEPENDS files...])
# Compiles every GLSL shader for the platform the build runs on and packs the results into one file:
#   all hosts except Apple : SPIR-V (Vulkan)
#   Windows               : + DXIL (D3D12), via SPIRV-Cross HLSL and DXC
#   Apple                 : one metallib holding every shader (Metal), via SPIRV-Cross MSL and the Xcode tools
# <build>/shaders/<pack>.pack is the shader library, loaded at run time (see render/shader_library.hpp).
# Target <pack>_shaders builds it; executables copy it next to themselves with obgi_install_shaders().
function(obgi_shader_pack out_pack)
  cmake_parse_arguments(ARG "" "NAME" "SHADERS;DEPENDS" ${ARGN})
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(entries "")
  set(entry_files "")
  set(airs "")
  foreach(src IN LISTS ARG_SHADERS)
    get_filename_component(base "${src}" NAME_WE)
    get_filename_component(ext "${src}" EXT)
    set(source "${PROJECT_SOURCE_DIR}/shaders/${src}")
    if(ext STREQUAL ".comp")
      set(dxc_profile cs_6_0)
    elseif(ext STREQUAL ".vert")
      set(dxc_profile vs_6_0)
      set(base "${base}_vert") # blit.vert and blit.frag must not share output names
    elseif(ext STREQUAL ".frag")
      set(dxc_profile ps_6_0)
      set(base "${base}_frag")
    else()
      message(FATAL_ERROR "unknown shader stage extension ${ext}")
    endif()

    set(spv "${dir}/${base}.spv")
    add_custom_command(
      OUTPUT "${spv}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
      COMMAND glslang-standalone -V --target-env vulkan1.1 "-I${PROJECT_SOURCE_DIR}/shaders" -o "${spv}" "${source}"
      DEPENDS "${source}" ${ARG_DEPENDS} glslang-standalone
      COMMENT "GLSL -> SPIR-V: ${src}" VERBATIM)
    if(NOT APPLE)
      list(APPEND entries "${base}:spirv:main:${spv}")
      list(APPEND entry_files "${spv}")
    endif()

    if(WIN32)
      set(hlsl "${dir}/${base}.hlsl")
      set(dxil "${dir}/${base}.dxil")
      add_custom_command(
        OUTPUT "${dxil}"
        COMMAND spirv-cross "${spv}" --hlsl --shader-model 60 --output "${hlsl}"
        COMMAND "${OBGI_DXC}" -nologo -T ${dxc_profile} -E main -O3 -Fo "${dxil}" "${hlsl}"
        DEPENDS "${spv}" spirv-cross
        COMMENT "SPIR-V -> HLSL -> DXIL: ${src}" VERBATIM)
      list(APPEND entries "${base}:dxil:main:${dxil}")
      list(APPEND entry_files "${dxil}")
    endif()

    if(APPLE)
      find_program(OBGI_XCRUN xcrun REQUIRED)
      set(msl "${dir}/${base}.metal")
      set(air "${dir}/${base}.air")
      add_custom_command(
        OUTPUT "${air}"
        COMMAND obgi_spvmsl "${spv}" "${msl}"
        COMMAND "${OBGI_XCRUN}" -sdk macosx metal -c "${msl}" -o "${air}"
        DEPENDS "${spv}" obgi_spvmsl
        COMMENT "SPIR-V -> MSL -> AIR: ${src}" VERBATIM)
      list(APPEND airs "${air}")
      list(APPEND entries "${base}:metallib:${base}:${dir}/${ARG_NAME}.metallib")
    endif()
  endforeach()

  if(APPLE)
    add_custom_command(
      OUTPUT "${dir}/${ARG_NAME}.metallib"
      COMMAND "${OBGI_XCRUN}" -sdk macosx metallib ${airs} -o "${dir}/${ARG_NAME}.metallib"
      DEPENDS ${airs}
      COMMENT "Linking ${ARG_NAME}.metallib" VERBATIM)
    list(APPEND entry_files "${dir}/${ARG_NAME}.metallib")
  endif()

  set(pack "${dir}/${ARG_NAME}.pack")
  add_custom_command(
    OUTPUT "${pack}"
    COMMAND obgi_shaderpack "${pack}" ${entries}
    DEPENDS ${entry_files} obgi_shaderpack
    COMMENT "Packing shader library ${ARG_NAME}.pack" VERBATIM)
  add_custom_target(${ARG_NAME}_shaders ALL DEPENDS "${pack}")
  set(${out_pack} "${pack}" PARENT_SCOPE)
endfunction()

# FreeType rasterizes the glyphs of the text engine. Optional codecs are off: only outline fonts are read.
function(obgi_fetch_freetype)
  if(TARGET freetype)
    return()
  endif()
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(freetype)
endfunction()


include("${CMAKE_CURRENT_LIST_DIR}/FFmpeg.cmake")
