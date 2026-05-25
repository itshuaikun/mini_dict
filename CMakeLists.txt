cmake_minimum_required(VERSION 3.16)

project(mini_dict C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MINI_DICT_NATIVE_OPTIMIZE "Optimize the binary for this machine" ON)
option(MINI_DICT_ENABLE_LTO "Enable link-time optimization for release builds" ON)

find_package(PkgConfig REQUIRED)
include(CheckIPOSupported)

if(MINI_DICT_ENABLE_LTO)
  check_ipo_supported(RESULT MINI_DICT_IPO_SUPPORTED OUTPUT MINI_DICT_IPO_ERROR LANGUAGES C)
  if(NOT MINI_DICT_IPO_SUPPORTED)
    message(WARNING "LTO is not supported by this toolchain: ${MINI_DICT_IPO_ERROR}")
  endif()
endif()

pkg_check_modules(APP_DEPS REQUIRED IMPORTED_TARGET
  gtk4
  libsoup-3.0
  json-glib-1.0
  sqlite3
  gstreamer-1.0
)

function(mini_dict_configure_target target)
  target_include_directories(${target} PRIVATE src)
  target_link_libraries(${target} PRIVATE PkgConfig::APP_DEPS)

  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(MINI_DICT_NATIVE_OPTIMIZE)
      target_compile_options(${target} PRIVATE
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-mtune=native>
      )
    endif()
  endif()

  if(MINI_DICT_ENABLE_LTO AND MINI_DICT_IPO_SUPPORTED)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
endfunction()

add_executable(mini-dict
  src/main.c
  src/app.c
  src/audio.c
  src/cache.c
  src/dictionary.c
)
mini_dict_configure_target(mini-dict)

enable_testing()

add_executable(test-dictionary-parser
  tests/test_dictionary.c
  src/dictionary.c
)
mini_dict_configure_target(test-dictionary-parser)

add_test(NAME dictionary-parser COMMAND test-dictionary-parser)

install(TARGETS mini-dict RUNTIME DESTINATION bin)
