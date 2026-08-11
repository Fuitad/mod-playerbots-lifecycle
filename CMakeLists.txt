if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(mod_playerbots_lifecycle LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  include(CTest)

  add_executable(playerbots_lifecycle_tests
    tests/cpp/PlayerbotLifecycleStandaloneTest.cpp
    src/Bot/Lifecycle/PlayerbotLifecycleConfig.cpp
    src/Bot/Lifecycle/RandomPlayerbotCleanupPlan.cpp)
  target_include_directories(playerbots_lifecycle_tests PRIVATE
    tests/standalone
    src)
  target_compile_options(playerbots_lifecycle_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME playerbots_lifecycle_tests COMMAND playerbots_lifecycle_tests)
else()
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/Bot/PlayerbotAI.h")
    message(FATAL_ERROR "mod-playerbots-lifecycle requires mod-playerbots")
  endif()

  AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")

  if(BUILD_TESTING)
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
      "${CMAKE_CURRENT_LIST_DIR}/tests/cpp/PlayerbotLifecycleIntegrationTest.cpp")
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
      "${CMAKE_CURRENT_LIST_DIR}/src")
  endif()
endif()
