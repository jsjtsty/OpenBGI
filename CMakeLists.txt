cmake_minimum_required(VERSION 3.28)
project(OpenBGI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENBGI_BUILD_TESTS "Build unit tests" ON)
option(OPENBGI_SANITIZE "Enable ASan/UBSan (GCC/Clang)" OFF)
set(OPENBGI_PROXY "" CACHE STRING "HTTP proxy used when downloading dependencies, e.g. http://127.0.0.1:7897")
set(OPENBGI_GAME_DIR "E:/Games/Amairo Chocolate 2" CACHE PATH "Game directory used by data-driven tests")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Proxy)
include(Warnings)
include(Deps)

add_subdirectory(src)

if(OPENBGI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
