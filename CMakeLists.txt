cmake_minimum_required(VERSION 3.16)
project(campfire VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(campfire
  src/app.cpp
  src/command_dispatcher.cpp
  src/command_handlers.cpp
  src/main.cpp
  src/library.cpp
  src/console_ui.cpp
  src/cli_args.cpp
)

target_include_directories(campfire PRIVATE include)

include(CTest)
if(BUILD_TESTING)
  add_executable(campfire_tests
    tests/campfire_tests.cpp
    src/app.cpp
    src/command_dispatcher.cpp
    src/command_handlers.cpp
    src/library.cpp
    src/console_ui.cpp
    src/cli_args.cpp
  )
  target_include_directories(campfire_tests PRIVATE include)
  add_test(NAME campfire_tests COMMAND campfire_tests)
endif()
