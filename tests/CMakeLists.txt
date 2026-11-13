include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE)
set(DOCTEST_NO_INSTALL ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # doctest declares an ancient cmake_minimum_required
FetchContent_MakeAvailable(doctest)

add_executable(obgi_tests main.cpp test_sha256.cpp test_dsc.cpp test_formats.cpp test_vm.cpp test_resources.cpp test_api.cpp)
target_include_directories(obgi_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(obgi_tests PRIVATE obgi_formats obgi_vfs obgi_vm obgi_programs doctest::doctest)
if(OPENBGI_BUILD_RUNTIME)
  target_sources(obgi_tests PRIVATE test_blend_gpu.cpp test_display.cpp test_background_fx.cpp test_vector_maps.cpp
    test_graphics_subops.cpp test_async_resources.cpp test_text.cpp test_audio.cpp test_movie.cpp test_system_subops.cpp test_runtime.cpp)
  target_link_libraries(obgi_tests PRIVATE SDL3::SDL3 obgi_app)
  target_compile_definitions(obgi_tests PRIVATE OPENBGI_HAS_APP_API)
  obgi_install_shaders(obgi_tests obgi_shaders "${OBGI_SHADER_PACK}")
else()
  target_sources(obgi_tests PRIVATE ../src/api/probe.cpp)
endif()

target_compile_definitions(obgi_tests PRIVATE
  OPENBGI_VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors"
  OPENBGI_GAME_DIR="${OPENBGI_GAME_DIR}")
obgi_target_defaults(obgi_tests)

add_test(NAME obgi_unit COMMAND obgi_tests --test-suite-exclude=game,gpu,host)
if(OPENBGI_BUILD_RUNTIME)
  add_test(NAME obgi_gpu COMMAND obgi_tests --test-suite=gpu)
  set_tests_properties(obgi_gpu PROPERTIES LABELS gpu)
  add_test(NAME obgi_host COMMAND obgi_tests --test-suite=host)
  set_tests_properties(obgi_host PROPERTIES LABELS host)
endif()
add_test(NAME obgi_game COMMAND obgi_tests --test-suite=game)
set_tests_properties(obgi_unit PROPERTIES LABELS unit)
set_tests_properties(obgi_game PROPERTIES LABELS game)
if(NOT EXISTS "${OPENBGI_GAME_DIR}/autoload.arc")
  set_tests_properties(obgi_game PROPERTIES DISABLED TRUE)
endif()
