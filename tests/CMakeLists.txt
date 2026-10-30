include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE)
set(DOCTEST_NO_INSTALL ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # doctest declares an ancient cmake_minimum_required
FetchContent_MakeAvailable(doctest)

add_executable(obgi_tests main.cpp test_sha256.cpp test_dsc.cpp test_formats.cpp test_vm.cpp test_blend_gpu.cpp test_display.cpp test_background_fx.cpp test_graphics_subops.cpp test_text.cpp test_audio.cpp test_movie.cpp test_system_subops.cpp)
target_link_libraries(obgi_tests PRIVATE SDL3::SDL3 obgi_formats obgi_vfs obgi_vm obgi_bindings obgi_render obgi_text obgi_audio obgi_video doctest::doctest)
target_compile_definitions(obgi_tests PRIVATE
  OPENBGI_VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors"
  OPENBGI_GAME_DIR="${OPENBGI_GAME_DIR}")
obgi_target_defaults(obgi_tests)

obgi_install_shaders(obgi_tests obgi_shaders "${OBGI_SHADER_PACK}")

add_test(NAME obgi_tests COMMAND obgi_tests)
