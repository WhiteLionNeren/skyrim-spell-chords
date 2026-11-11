cmake_minimum_required(VERSION 3.21)
project(SpellChords VERSION 1.1.6 LANGUAGES CXX)
# CommonLibSSE-NG (alandtse/CommonLibVR, tag v8.3.0) is the git submodule extern/CommonLibSSE; -DCommonLibPath=... overrides it.
# The SKSE version block is written by hand in src/main.cpp (CommonLib's add_commonlibsse_plugin helper isn't available
# for a source build).
set(CommonLibPath "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE" CACHE PATH "CommonLibSSE-NG source tree")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(SKYRIMFLATRIM _CRT_SECURE_NO_WARNINGS _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
add_subdirectory(${CommonLibPath} CommonLibSSE)
add_library(${PROJECT_NAME} SHARED src/main.cpp src/bar.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE include src)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE <RE/Skyrim.h> <SKSE/SKSE.h>)
