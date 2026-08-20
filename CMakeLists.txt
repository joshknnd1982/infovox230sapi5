cmake_minimum_required(VERSION 3.20)
project(Infovox230SAPI VERSION 1.0.0 LANGUAGES CXX)

# The Infovox 230 engine is a 32-bit dll, so the worker that owns it is always
# x86. The SAPI5 engine itself is built twice -- 32-bit for 32-bit hosts and
# 64-bit for hosts like Narrator -- and both are thin clients of the one worker.
# Configure this file once per architecture; build_all.bat does both and stages
# the result for the installer.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets MSVC; SAPI5 and the engine are Windows-only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(IVX_X64 TRUE)
    message(STATUS "Infovox230SAPI: 64-bit SAPI5 engine")
else()
    set(IVX_X64 FALSE)
    message(STATUS "Infovox230SAPI: 32-bit SAPI5 engine, worker and diagnostics")
endif()

add_compile_options(/W4 /MP /permissive- /EHsc)
add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
# Statically linked so a missing Visual C++ redistributable can never be the
# reason a screen reader has no voice.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Shared by every target.
# ---------------------------------------------------------------------------
add_library(ivx_common STATIC
    src/ivx_log.cpp
    src/ivx_paths.cpp
    src/ivx_catalog.cpp
    src/ivx_vregistry.cpp
)
target_include_directories(ivx_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ivx_common PUBLIC shlwapi advapi32)

# ---------------------------------------------------------------------------
# The SAPI5 engine: built for both architectures.
# ---------------------------------------------------------------------------
add_library(Infovox230SAPI SHARED
    src/ivx_sapi_main.cpp
    src/ivx_sapi_tokens.cpp
    src/ivx_sapi_engine.cpp
    src/ivx_client.cpp
    src/ivx_com.cpp
    src/Infovox230SAPI.def
)
target_link_libraries(Infovox230SAPI PRIVATE ivx_common ole32 oleaut32)
set_target_properties(Infovox230SAPI PROPERTIES OUTPUT_NAME "Infovox230SAPI")

# Drives the finished engine through SAPI5 itself rather than through our own
# interfaces, which is the only way to test what applications actually do. Built
# for both architectures because the 64-bit path is the one that crosses a
# process boundary.
add_executable(Infovox230SapiTest tools/ivx_sapi_test.cpp)
target_link_libraries(Infovox230SapiTest PRIVATE ole32 oleaut32)
target_include_directories(Infovox230SapiTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# 32-bit only: the worker that owns the engine, and the diagnostics tool.
# ---------------------------------------------------------------------------
if(NOT IVX_X64)
    add_library(ivx_engine STATIC src/ivx_engine.cpp)
    target_link_libraries(ivx_engine PUBLIC ivx_common ole32 oleaut32 winmm)

    add_executable(Infovox230Server src/ivx_server.cpp)
    target_link_libraries(Infovox230Server PRIVATE ivx_engine)
endif()

# Diagnostics are built for both architectures. The 32-bit one can drive the
# engine directly as well as through the worker; the 64-bit one can only go
# through the worker, which is precisely the path it needs to prove.
add_executable(Infovox230Diag tools/ivx_probe.cpp src/ivx_client.cpp)
if(IVX_X64)
    target_link_libraries(Infovox230Diag PRIVATE ivx_common ole32 oleaut32)
else()
    target_link_libraries(Infovox230Diag PRIVATE ivx_engine)
    target_compile_definitions(Infovox230Diag PRIVATE IVX_HAVE_ENGINE)
endif()

# ---------------------------------------------------------------------------
# Staging: everything the installer packages, in the layout it installs.
# ---------------------------------------------------------------------------
set(IVX_STAGE ${CMAKE_CURRENT_SOURCE_DIR}/output)
set(IVX_ENGINE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bin/infovox230/engine)

if(IVX_X64)
    add_custom_command(TARGET Infovox230SAPI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IVX_STAGE}/x64
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230SAPI> ${IVX_STAGE}/x64/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230Diag> ${IVX_STAGE}/x64/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230SapiTest> ${IVX_STAGE}/x64/
        COMMENT "Staging the 64-bit SAPI5 engine")
else()
    add_custom_command(TARGET Infovox230Server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IVX_STAGE}/engine
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230SAPI> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230Server> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230Diag> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox230SapiTest> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${IVX_ENGINE_SOURCE} ${IVX_STAGE}/engine
        COMMENT "Staging the 32-bit engine, worker and voice data")
endif()
