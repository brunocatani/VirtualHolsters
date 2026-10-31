cmake_minimum_required(VERSION 3.24)

set(NAME "VirtualHolsters")
set(VERSION 3.0.8)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../../.." ABSOLUTE)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
  if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} "$ENV{${VARIABLE}}")
  endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "")
  message(FATAL_ERROR "VCPKG_ROOT is not set")
endif()

set(
  CMAKE_TOOLCHAIN_FILE
  "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
  CACHE STRING ""
)
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(
  CMAKE_MSVC_RUNTIME_LIBRARY
  "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  CACHE STRING ""
)

if(DEFINED COMMON_LIB_F4VR_PATH AND NOT COMMON_LIB_F4VR_PATH STREQUAL "")
  set(COMMON_LIB_F4VR_PATH_SELECTED "${COMMON_LIB_F4VR_PATH}")
else()
  set(
    COMMON_LIB_F4VR_PATH_SELECTED
    "${WORKSPACE_ROOT}/libraries_and_tools/CommonLibF4VR/CommonLibF4"
  )
endif()
if(NOT EXISTS "${COMMON_LIB_F4VR_PATH_SELECTED}/include/REL/Relocation.h")
  message(
    FATAL_ERROR
    "CommonLibF4VR was not found at '${COMMON_LIB_F4VR_PATH_SELECTED}'"
  )
endif()

if(DEFINED LEGACY_F4SEVR_PATH AND NOT LEGACY_F4SEVR_PATH STREQUAL "")
  set(LEGACY_F4SEVR_PATH_SELECTED "${LEGACY_F4SEVR_PATH}")
else()
  set(
    LEGACY_F4SEVR_PATH_SELECTED
    "${WORKSPACE_ROOT}/libraries_and_tools/f4sevr_0_6_21/src/f4sevr"
  )
endif()
if(NOT EXISTS "${LEGACY_F4SEVR_PATH_SELECTED}/f4se/PluginAPI.h")
  message(
    FATAL_ERROR
    "The transitional F4SEVR source ABI was not found at "
    "'${LEGACY_F4SEVR_PATH_SELECTED}'"
  )
endif()
get_filename_component(
  LEGACY_F4SEVR_SOURCE_ROOT
  "${LEGACY_F4SEVR_PATH_SELECTED}/.."
  ABSOLUTE
)
if(NOT EXISTS "${LEGACY_F4SEVR_SOURCE_ROOT}/common/IPrefix.h")
  message(
    FATAL_ERROR
    "The transitional F4SEVR common headers were not found under "
    "'${LEGACY_F4SEVR_SOURCE_ROOT}'"
  )
endif()

# ROCK is optional at runtime; its public major-2 headers are a build input so
# the plugin can bind ROCK.dll when it is loaded.
set(
  ROCK_INCLUDE_DIR
  ""
  CACHE PATH
  "ROCK's public include directory (the folder containing ROCK/Client.h)."
)
if(NOT EXISTS "${ROCK_INCLUDE_DIR}/ROCK/Client.h")
  message(
    FATAL_ERROR
    "ROCK_INCLUDE_DIR must name ROCK's include directory (ROCK/Client.h was "
    "not found under '${ROCK_INCLUDE_DIR}')"
  )
endif()

set(OPENVR_ROOT "${ROOT_DIR}/openvr")
if(
  NOT EXISTS "${OPENVR_ROOT}/openvr.h"
  OR NOT EXISTS "${OPENVR_ROOT}/openvr_api.lib"
  OR NOT EXISTS "${OPENVR_ROOT}/LICENSE"
)
  message(
    FATAL_ERROR
    "The independently vendored OpenVR ABI is incomplete under '${OPENVR_ROOT}'"
  )
endif()

foreach(
  LEGACY_LIBRARY
  IN ITEMS
    "${ROOT_DIR}/lib/f4sevr_1_2_72.lib"
    "${ROOT_DIR}/lib/f4se_common.lib"
    "${ROOT_DIR}/lib/common_vc11.lib"
)
  if(NOT EXISTS "${LEGACY_LIBRARY}")
    message(FATAL_ERROR "Required transitional library is missing: ${LEGACY_LIBRARY}")
  endif()
endforeach()

project(${NAME} VERSION ${VERSION} LANGUAGES CXX RC)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
set(F4SE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(
  "${COMMON_LIB_F4VR_PATH_SELECTED}"
  "${CMAKE_CURRENT_BINARY_DIR}/CommonLibF4"
  EXCLUDE_FROM_ALL
)
set_property(
  TARGET CommonLibF4
  PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
)

add_library(OpenVR::OpenVR STATIC IMPORTED GLOBAL)
set_target_properties(
  OpenVR::OpenVR
  PROPERTIES
    IMPORTED_LOCATION "${OPENVR_ROOT}/openvr_api.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENVR_ROOT}"
)

add_library(
  ${PROJECT_NAME}
  SHARED
  "BSFlattenedBoneTree.cpp"
  "ConfigModeMenu.cpp"
  "Holsters.cpp"
  "InvSorting.cpp"
  "LegacyBridge.cpp"
  "Matrix.cpp"
  "MenuChecker.cpp"
  "MiscStructs.cpp"
  "NativeEquipment.cpp"
  "Offsets.cpp"
  "RockProvider.cpp"
  "Utils.cpp"
  "VRHook.cpp"
  "VRSceneGraph.cpp"
  "hook.cpp"
  "main.cpp"
  "weaponOffset.cpp"
  "VirtualHolsters.rc"
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMMNOSOUND
)
target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
    "${ROOT_DIR}"
    "${ROOT_DIR}/include"
    "${OPENVR_ROOT}"
)
set_source_files_properties(
  "RockProvider.cpp"
  PROPERTIES
    INCLUDE_DIRECTORIES "${ROCK_INCLUDE_DIR}"
    COMPILE_DEFINITIONS NOMINMAX
)
set_source_files_properties(
  "NativeEquipment.cpp"
  PROPERTIES
    COMPILE_DEFINITIONS NOMINMAX
)
target_compile_options(
  ${PROJECT_NAME}
  PRIVATE
    /sdl
    /utf-8
    /Zi
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
    /wd4100
    /wd4189
    /wd4244
    /wd4267
    /wd4456
    /wd4458
    /wd4701
    /wd4715
    "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
)

set(
  LEGACY_GAMEPLAY_SOURCES
  "BSFlattenedBoneTree.cpp"
  "ConfigModeMenu.cpp"
  "Holsters.cpp"
  "InvSorting.cpp"
  "LegacyBridge.cpp"
  "Matrix.cpp"
  "MenuChecker.cpp"
  "MiscStructs.cpp"
  "Offsets.cpp"
  "Utils.cpp"
  "VRHook.cpp"
  "weaponOffset.cpp"
)
set_source_files_properties(
  ${LEGACY_GAMEPLAY_SOURCES}
  PROPERTIES
    COMPILE_OPTIONS
      "/I${LEGACY_F4SEVR_SOURCE_ROOT};/I${LEGACY_F4SEVR_PATH_SELECTED};/FI${LEGACY_F4SEVR_SOURCE_ROOT}/common/IPrefix.h;/permissive;/Zc:preprocessor-;/std:c++17"
)
set_source_files_properties(
  "hook.cpp"
  "main.cpp"
  PROPERTIES
    COMPILE_DEFINITIONS NOMINMAX
)
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
    CommonLibF4::CommonLibF4
    OpenVR::OpenVR
    "${ROOT_DIR}/lib/f4sevr_1_2_72.lib"
    "${ROOT_DIR}/lib/f4se_common.lib"
    "${ROOT_DIR}/lib/common_vc11.lib"
    shell32.lib
    version.lib
)
target_link_options(
  ${PROJECT_NAME}
  PRIVATE
    "/DEF:${ROOT_DIR}/exports.def"
    "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
    "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION FALSE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
)

add_executable(
  VirtualHolstersInteractionTests
  "tests/HolsterInteractionTests.cpp"
)
target_compile_features(
  VirtualHolstersInteractionTests
  PRIVATE
    cxx_std_23
)
target_include_directories(
  VirtualHolstersInteractionTests
  PRIVATE
    "${ROOT_DIR}"
)
target_compile_options(
  VirtualHolstersInteractionTests
  PRIVATE
    /sdl
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
)

enable_testing()
add_test(
  NAME VirtualHolstersInteractionTests
  COMMAND VirtualHolstersInteractionTests
)

option(
  POST_BUILD_COPY_PLUGIN
  "Copy VirtualHolsters.dll to a mod root."
  OFF
)
set(
  COPY_PLUGIN_BASE_PATH
  ""
  CACHE PATH
  "Mod root containing F4SE/Plugins."
)
if(POST_BUILD_COPY_PLUGIN)
  if(COPY_PLUGIN_BASE_PATH STREQUAL "")
    message(
      FATAL_ERROR
      "COPY_PLUGIN_BASE_PATH is required when POST_BUILD_COPY_PLUGIN is enabled"
    )
  endif()
  add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND}
      -E
      make_directory
      "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins"
    COMMAND
      ${CMAKE_COMMAND}
      -E
      copy_if_different
      "$<TARGET_FILE:${PROJECT_NAME}>"
      "${COPY_PLUGIN_BASE_PATH}/F4SE/Plugins/"
    COMMENT "Deploying VirtualHolsters.dll to '${COPY_PLUGIN_BASE_PATH}'"
  )
endif()

# Tests share build-fast but stay out of the default build, so building and deploying the
# plugin never compiles them. Build them with `--target build_tests`, then run ctest.
function(fo4dev_collect_targets directory result)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${child}" NORMALIZE child_is_ours)
    if(child_is_ours)
      fo4dev_collect_targets("${child}" child_targets)
      list(APPEND targets ${child_targets})
    endif()
  endforeach()
  set(${result} ${targets} PARENT_SCOPE)
endfunction()
fo4dev_collect_targets("${CMAKE_SOURCE_DIR}" fo4dev_targets)
add_custom_target(build_tests)
foreach(fo4dev_target IN LISTS fo4dev_targets)
  get_target_property(fo4dev_target_type ${fo4dev_target} TYPE)
  string(TOLOWER "${fo4dev_target}" fo4dev_target_name)
  if(fo4dev_target_name MATCHES "test" AND fo4dev_target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
    set_target_properties(${fo4dev_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    if(fo4dev_target_type STREQUAL "EXECUTABLE")
      add_dependencies(build_tests ${fo4dev_target})
    endif()
  endif()
endforeach()
