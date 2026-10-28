# FFmpeg: every audio/video format the engine reads. FFmpeg has no CMake build, so its own configure/make is
# driven from here (cmake/ffmpeg_build.sh.in; on Windows through MSYS2 + the Visual Studio toolchain).
#
# Licensing: configure is run WITHOUT --enable-gpl, --enable-version3 and --enable-nonfree and with every
# external library off (--disable-autodetect), so the result is LGPL v2.1-or-later only; the test suite checks
# avutil_license() at run time. The libraries are linked statically (allowed for LGPL when the project's
# sources and object files stay available for relinking).
#
# Everything unused is compiled out: start from --disable-everything and enable only the lists below. To
# support a new format, add its decoder / demuxer / parser names to these cache variables (ffmpeg -decoders,
# -demuxers and -parsers list the names) and rebuild; nothing else changes.
set(OBGI_FFMPEG_TAG "n9.0.2" CACHE STRING "FFmpeg release tag to build")
set(OBGI_FFMPEG_DECODERS
  vorbis flac opus mp1 mp2 mp3 aac
  pcm_s16le pcm_s24le pcm_u8 pcm_f32le adpcm_ms adpcm_ima_wav
  mpeg1video mpeg2video
  CACHE STRING "FFmpeg decoders to build (semicolon separated)")
# mpegvideo (raw MPEG video) is not for playing files: the MPEG program stream demuxer probes its video stream with it
set(OBGI_FFMPEG_DEMUXERS ogg flac wav mp3 aac mpegps mpegvideo mpegts matroska
  CACHE STRING "FFmpeg demuxers to build")
set(OBGI_FFMPEG_PARSERS vorbis flac opus mpegaudio mpegvideo aac
  CACHE STRING "FFmpeg parsers to build")
set(OBGI_MSYS2_ROOT "" CACHE PATH "MSYS2 installation that runs FFmpeg's configure/make on Windows (auto-detected)")

function(obgi_fetch_ffmpeg)
  if(TARGET FFmpeg::avcodec)
    return()
  endif()
  FetchContent_Declare(ffmpeg
    GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
    GIT_TAG ${OBGI_FFMPEG_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(ffmpeg) # FFmpeg has no CMakeLists.txt: this only downloads the source

  set(prefix "${CMAKE_BINARY_DIR}/ffmpeg-install")
  set(build_dir "${CMAKE_BINARY_DIR}/ffmpeg-build")

  set(args
    --prefix=__PREFIX__
    --enable-static --disable-shared
    --disable-everything --disable-autodetect
    --disable-programs --disable-doc --disable-network --disable-debug
    --disable-avdevice --disable-avfilter
    --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --enable-swscale
    --enable-protocol=file)
  string(REPLACE ";" "," decoders "${OBGI_FFMPEG_DECODERS}")
  string(REPLACE ";" "," demuxers "${OBGI_FFMPEG_DEMUXERS}")
  string(REPLACE ";" "," parsers "${OBGI_FFMPEG_PARSERS}")
  list(APPEND args "--enable-decoder=${decoders}" "--enable-demuxer=${demuxers}" "--enable-parser=${parsers}")

  set(prelude "")
  if(WIN32)
    # configure and make run inside MSYS2 with the Visual Studio compiler on PATH
    set(search_roots "${OBGI_MSYS2_ROOT}" "$ENV{MSYS2_ROOT}" "C:/msys64" "D:/msys64" "C:/tools/msys64")
    set(msys "")
    foreach(root IN LISTS search_roots)
      if(root AND EXISTS "${root}/usr/bin/bash.exe")
        set(msys "${root}")
        break()
      endif()
    endforeach()
    if(NOT msys)
      message(FATAL_ERROR "FFmpeg needs MSYS2 on Windows. Install it from https://www.msys2.org and set OBGI_MSYS2_ROOT (default C:/msys64).")
    endif()
    if(NOT EXISTS "${msys}/usr/bin/make.exe" OR NOT EXISTS "${msys}/usr/bin/diff.exe")
      message(FATAL_ERROR "MSYS2 at ${msys} lacks make/diffutils. In an MSYS2 shell run:  pacman -S --needed make diffutils")
    endif()
    find_program(OBGI_NASM nasm HINTS "C:/Program Files/NASM" "$ENV{LOCALAPPDATA}/bin/NASM" "$ENV{ProgramFiles}/NASM")
    if(NOT OBGI_NASM)
      message(FATAL_ERROR "FFmpeg needs NASM on Windows: install it (https://www.nasm.us) or pass -DOBGI_NASM=<path to nasm.exe>.")
    endif()
    get_filename_component(nasm_dir "${OBGI_NASM}" DIRECTORY)

    if(CMAKE_GENERATOR_INSTANCE)
      set(vs_root "${CMAKE_GENERATOR_INSTANCE}")
    else()
      string(REGEX REPLACE "/VC/Tools/.*$" "" vs_root "${CMAKE_C_COMPILER}")
    endif()
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
      set(vcvars_name vcvarsarm64.bat)
      set(ff_arch aarch64)
    else()
      set(vcvars_name vcvars64.bat)
      set(ff_arch x86_64)
    endif()
    set(vcvars "${vs_root}/VC/Auxiliary/Build/${vcvars_name}")
    if(NOT EXISTS "${vcvars}")
      message(FATAL_ERROR "cannot find ${vcvars_name} under ${vs_root}; the FFmpeg build needs the Visual Studio C++ toolchain")
    endif()
    # FFmpeg defaults to the static CRT (/MT); the engine links the DLL CRT (/MD)
    list(APPEND args --extra-cflags=-MD --toolchain=msvc --arch=${ff_arch} --target-os=win64 --x86asmexe=nasm)
    set(OBGI_FF_VCVARS "${vcvars}")
    set(OBGI_FF_NASM_DIR "${nasm_dir}")
    set(OBGI_FF_MSYS_BASH "${msys}/usr/bin/bash.exe")
    # the MSVC link.exe must win over MSYS's own link command
    set(prelude "export PATH=\"$(dirname \"$(command -v cl)\"):$PATH\"")
  else()
    list(APPEND args --enable-pic)
  endif()

  string(REPLACE ";" " " arg_string "${args}")
  set(OBGI_FF_SRC "${ffmpeg_SOURCE_DIR}")
  set(OBGI_FF_BUILD "${build_dir}")
  set(OBGI_FF_PREFIX "${prefix}")
  set(OBGI_FF_ARGS "${arg_string}")
  set(OBGI_FF_PRELUDE "${prelude}")
  cmake_host_system_information(RESULT OBGI_FF_JOBS QUERY NUMBER_OF_LOGICAL_CORES)
  configure_file("${PROJECT_SOURCE_DIR}/cmake/ffmpeg_build.sh.in" "${CMAKE_BINARY_DIR}/ffmpeg_build.sh" @ONLY NEWLINE_STYLE UNIX)
  if(WIN32)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/ffmpeg_build.bat.in" "${CMAKE_BINARY_DIR}/ffmpeg_build.bat" @ONLY NEWLINE_STYLE WIN32)
    set(build_cmd "${CMAKE_BINARY_DIR}/ffmpeg_build.bat")
  else()
    set(build_cmd sh "${CMAKE_BINARY_DIR}/ffmpeg_build.sh")
  endif()

  set(libs avformat avcodec swscale swresample avutil)
  set(byproducts "")
  foreach(lib IN LISTS libs)
    if(MSVC)
      list(APPEND byproducts "${prefix}/lib/${lib}.lib")
    else()
      list(APPEND byproducts "${prefix}/lib/lib${lib}.a")
    endif()
  endforeach()

  include(ExternalProject)
  ExternalProject_Add(ffmpeg_build
    SOURCE_DIR "${ffmpeg_SOURCE_DIR}"
    BINARY_DIR "${build_dir}"
    INSTALL_DIR "${prefix}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${build_cmd}
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE # incremental: the script only reconfigures when the argument list changed
    BUILD_BYPRODUCTS ${byproducts}
    USES_TERMINAL_BUILD TRUE)

  file(MAKE_DIRECTORY "${prefix}/include") # imported targets need the directory to exist at configure time
  if(WIN32)
    set(system_libs bcrypt ole32 user32 advapi32 shell32)
  elseif(APPLE)
    set(system_libs "-framework CoreFoundation" "-framework CoreVideo" "-framework Security" m)
  else()
    set(system_libs m pthread)
  endif()
  foreach(lib IN LISTS libs)
    add_library(FFmpeg::${lib} STATIC IMPORTED GLOBAL)
    if(MSVC)
      set(file "${prefix}/lib/${lib}.lib")
    else()
      set(file "${prefix}/lib/lib${lib}.a")
    endif()
    set_target_properties(FFmpeg::${lib} PROPERTIES
      IMPORTED_LOCATION "${file}"
      INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include")
    add_dependencies(FFmpeg::${lib} ffmpeg_build)
  endforeach()
  # static libraries must follow dependency order: avformat > avcodec > swscale/swresample > avutil
  set_property(TARGET FFmpeg::avformat PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avcodec)
  set_property(TARGET FFmpeg::avcodec PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::swresample FFmpeg::avutil)
  set_property(TARGET FFmpeg::swscale PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avutil)
  set_property(TARGET FFmpeg::swresample PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::avutil)
  set_property(TARGET FFmpeg::avutil PROPERTY INTERFACE_LINK_LIBRARIES ${system_libs})
endfunction()
