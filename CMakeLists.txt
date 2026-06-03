cmake_minimum_required(VERSION 3.16)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(mini_dict VERSION 0.1.0 LANGUAGES C)

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
  gtk4-layer-shell-0
  libsoup-3.0
  json-glib-1.0
  sqlite3
  gstreamer-1.0
)

pkg_check_modules(WEBKITGTK REQUIRED IMPORTED_TARGET webkitgtk-6.0)
find_program(CARGO_EXECUTABLE cargo)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(MINI_DICT_CARGO_PROFILE release)
  set(MINI_DICT_CARGO_BUILD_ARGS --release)
else()
  set(MINI_DICT_CARGO_PROFILE debug)
  set(MINI_DICT_CARGO_BUILD_ARGS)
endif()

set(MINI_DICT_LDOCE_READER_MANIFEST ${CMAKE_SOURCE_DIR}/rust/ldoce_reader/Cargo.toml)
set(MINI_DICT_LDOCE_READER_LIB
    ${CMAKE_BINARY_DIR}/cargo/${MINI_DICT_CARGO_PROFILE}/libmini_dict_ldoce_reader.a)

function(mini_dict_configure_target target)
  target_include_directories(${target} PRIVATE src)
  target_link_libraries(${target} PRIVATE PkgConfig::APP_DEPS)
  target_compile_definitions(${target} PRIVATE
    MINI_DICT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    MINI_DICT_VERSION="${PROJECT_VERSION}"
  )

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
  src/chinese_index.c
  src/dictionary.c
  src/dictionary_page_link.c
  src/local_dictionary.c
)
mini_dict_configure_target(mini-dict)

target_compile_definitions(mini-dict PRIVATE MINI_DICT_HAVE_WEBKITGTK)
target_link_libraries(mini-dict PRIVATE PkgConfig::WEBKITGTK)

if(CARGO_EXECUTABLE)
  add_custom_command(
    OUTPUT ${MINI_DICT_LDOCE_READER_LIB}
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/cargo
            ${CARGO_EXECUTABLE} build
            --manifest-path ${MINI_DICT_LDOCE_READER_MANIFEST}
            ${MINI_DICT_CARGO_BUILD_ARGS}
    DEPENDS
      ${MINI_DICT_LDOCE_READER_MANIFEST}
      ${CMAKE_SOURCE_DIR}/rust/ldoce_reader/src/lib.rs
    VERBATIM
  )
  add_custom_target(mini-dict-ldoce-reader DEPENDS ${MINI_DICT_LDOCE_READER_LIB})

  foreach(target mini-dict)
    add_dependencies(${target} mini-dict-ldoce-reader)
    target_compile_definitions(${target} PRIVATE MINI_DICT_HAVE_LDOCE_READER)
    target_link_libraries(${target} PRIVATE ${MINI_DICT_LDOCE_READER_LIB} dl pthread m)
  endforeach()
else()
  message(WARNING "cargo was not found; local MDX/MDD lookup will be disabled")
endif()

enable_testing()

add_executable(test-dictionary-parser
  tests/test_dictionary.c
  src/dictionary.c
)
mini_dict_configure_target(test-dictionary-parser)

add_test(NAME dictionary-parser COMMAND test-dictionary-parser)

add_executable(test-dictionary-page-link
  tests/test_dictionary_page_link.c
  src/dictionary_page_link.c
)
mini_dict_configure_target(test-dictionary-page-link)

add_test(NAME dictionary-page-link COMMAND test-dictionary-page-link)

add_executable(test-local-dictionary
  tests/test_local_dictionary.c
  src/local_dictionary.c
)
mini_dict_configure_target(test-local-dictionary)

if(CARGO_EXECUTABLE)
  add_dependencies(test-local-dictionary mini-dict-ldoce-reader)
  target_compile_definitions(test-local-dictionary PRIVATE MINI_DICT_HAVE_LDOCE_READER)
  target_link_libraries(test-local-dictionary PRIVATE ${MINI_DICT_LDOCE_READER_LIB} dl pthread m)
endif()

add_test(NAME local-dictionary COMMAND test-local-dictionary)

install(TARGETS mini-dict RUNTIME DESTINATION bin)
