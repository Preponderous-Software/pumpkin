cmake_minimum_required(VERSION 3.19)
project(pumpkin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PUMPKIN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PUMPKIN_BUILD_TESTS "Build the test suite" ON)
option(PUMPKIN_WEB "Build the browser toy (Emscripten + Strata's WebGL 2 backend)" OFF)

# Emscripten: the carving rules and tests build as they are and the tests run
# under Node; PUMPKIN_WEB adds the page. Strata compiles C++ exceptions as
# wasm exceptions, and everything in one .wasm must match.
if(EMSCRIPTEN)
  cmake_minimum_required(VERSION 3.24)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>")
  add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1)
endif()
if(PUMPKIN_WEB AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "PUMPKIN_WEB needs Emscripten (emcmake cmake ...)")
endif()

function(pumpkin_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(PUMPKIN_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(PUMPKIN_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Carving: the pieces, the cuts, undo, links, and the pumpkin's shape and
# pictures. No engine and no GPU.
add_library(pumpkin_carve STATIC
  src/carve/carving.cpp
  src/carve/editor.cpp
  src/carve/mask.cpp
  src/carve/pumpkin.cpp
  src/carve/shapes.cpp)
target_include_directories(pumpkin_carve PUBLIC src)
pumpkin_warnings(pumpkin_carve)

if(PUMPKIN_BUILD_TESTS)
  enable_testing()
  foreach(t link carve)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE pumpkin_carve)
    target_include_directories(test_${t} PRIVATE tests)
    pumpkin_warnings(test_${t})
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()

# ---------------------------------------------------------------- the toy on Strata
# Strata is fetched at PUMPKIN_STRATA_REF (the v0.7.0 tag: ABI 1.12), or taken
# from a local checkout with -DFETCHCONTENT_SOURCE_DIR_STRATA=<path>.
if(PUMPKIN_WEB)
  include(FetchContent)
  set(PUMPKIN_STRATA_REF "20827c3d67e71c1f942c29e7cf8adfa4801188a8" CACHE STRING "Strata git ref (a full commit SHA)")
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_SIM OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_USD OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${PUMPKIN_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  add_executable(pumpkin-web app/web_main.cpp src/view/view.cpp)
  target_link_libraries(pumpkin-web PRIVATE pumpkin_carve strata::static)
  pumpkin_warnings(pumpkin-web)
  set_target_properties(pumpkin-web PROPERTIES OUTPUT_NAME pumpkin SUFFIX ".html")
  target_link_options(pumpkin-web PRIVATE
    -sSTACK_SIZE=1MB
    "-sEXPORTED_FUNCTIONS=_main,_pumpkin_pointer,_pumpkin_stamp,_pumpkin_edit,_pumpkin_undo,_pumpkin_start_over,_pumpkin_variety,_pumpkin_dark,_pumpkin_layout,_pumpkin_mode,_pumpkin_load,_pumpkin_code,_pumpkin_porch,_pumpkin_porch_choose,_pumpkin_snapshot,_pumpkin_snapshot_width,_pumpkin_snapshot_height,_pumpkin_shape_count,_pumpkin_shape_part,_pumpkin_shape_svg,_pumpkin_screen_of,_pumpkin_status"
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  set_property(TARGET pumpkin-web APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  # The loading shell (web/strata-kit, vendored from Strata) and its bridge go
  # inline into the page, with the download's size (web/strata_page.cmake).
  set_property(TARGET pumpkin-web APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/strata-kit/strata-loader.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/strata-boot.js")
  add_custom_command(TARGET pumpkin-web POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DHTML=$<TARGET_FILE:pumpkin-web>"
            "-DWASM=$<TARGET_FILE_DIR:pumpkin-web>/$<TARGET_FILE_BASE_NAME:pumpkin-web>.wasm"
            "-DWEB=${CMAKE_CURRENT_SOURCE_DIR}/web" -P "${CMAKE_CURRENT_SOURCE_DIR}/web/strata_page.cmake"
    VERBATIM)
endif()
