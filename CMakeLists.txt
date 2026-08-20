cmake_minimum_required(VERSION 3.15)
project(BestspeechSAPI VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUILD_X64 TRUE)
    message(STATUS "Building x64 version")
else()
    set(BUILD_X64 FALSE)
    message(STATUS "Building x86 version")
endif()

if(NOT BUILD_X64)
    add_library(pipe_client STATIC
        src/pipe_client.cpp
    )

    target_include_directories(pipe_client PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(pipe_client PUBLIC shlwapi)

    target_compile_definitions(pipe_client PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET pipe_client PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_library(b32_wrapper STATIC
        src/b32_wrapper.cpp
    )

    target_include_directories(b32_wrapper PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # b32_wrapper.dll is resolved at runtime from the install directory, so nothing
    # is linked against it here.

    target_compile_definitions(b32_wrapper PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    add_library(BestspeechSAPI SHARED
        src/sapi_main.cpp
        src/com.cpp
        src/registry.cpp
        src/ISpDataKeyImpl.cpp
        src/IEnumSpObjectTokensImpl.cpp
        src/ISpTTSEngineImpl.cpp
        src/helper_client.cpp
        src/text_pipeline.cpp
        src/voice_token.cpp
        src/bestspeech_sapi.def
    )

    target_include_directories(BestspeechSAPI PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(BestspeechSAPI PRIVATE
        b32_wrapper
        pipe_client
        ole32
        oleaut32
        advapi32
        shell32
    )

    target_compile_definitions(BestspeechSAPI PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    add_executable(BestspeechServer
        src/bestspeech_server.cpp
    )

    target_include_directories(BestspeechServer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(BestspeechServer PRIVATE
        b32_wrapper
        shlwapi
    )

    target_compile_definitions(BestspeechServer PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        target_compile_options(b32_wrapper PRIVATE /W3 /MP)
        target_compile_options(BestspeechSAPI PRIVATE /W3 /MP)
        target_compile_options(BestspeechServer PRIVATE /W3 /MP)

        set_property(TARGET b32_wrapper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_property(TARGET BestspeechSAPI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_property(TARGET BestspeechServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Verification tooling: prints what the text pipeline hands each engine, so its
    # output can be diffed against tools/translit_ref.py and fed into the real dll.
    add_executable(pipeline_probe
        tools/pipeline_probe.cpp
        src/text_pipeline.cpp
    )

    target_include_directories(pipeline_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_compile_definitions(pipeline_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET pipeline_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_executable(sapi_probe tools/sapi_probe.cpp)

    target_include_directories(sapi_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(sapi_probe PRIVATE ole32 oleaut32)

    target_compile_definitions(sapi_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET sapi_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Registers the voices for the current user and drives them through SAPI itself,
    # which is the one path the other probes cannot reach.
    add_executable(token_probe
        tools/token_probe.cpp
        src/registry.cpp
    )

    target_include_directories(token_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(token_probe PRIVATE ole32 oleaut32 advapi32 sapi)

    target_compile_definitions(token_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    add_executable(multi_engine_probe
        tools/multi_engine_probe.cpp
        src/b32_wrapper.cpp
    )

    target_include_directories(multi_engine_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(multi_engine_probe PRIVATE advapi32)

    target_compile_definitions(multi_engine_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET token_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_property(TARGET multi_engine_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Shipped with the product: a user-runnable check that walks every installed voice
    # through SAPI and writes a plain text report, for when a voice will not speak.
    # Hammers the engine with utterances the host abandons part way through, which is
    # what a screen reader does constantly. A cancel that leaves the helper protocol out
    # of step shows up here as a hang or as silence on the utterances that follow.
    add_executable(cancel_probe tools/cancel_probe.cpp)

    target_include_directories(cancel_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(cancel_probe PRIVATE ole32 oleaut32)

    target_compile_definitions(cancel_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET cancel_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_executable(BestSpeechDiagnostics
        tools/bestspeech_diag.cpp
        src/registry.cpp
    )

    target_include_directories(BestSpeechDiagnostics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(BestSpeechDiagnostics PRIVATE ole32 oleaut32 advapi32 sapi)

    target_compile_definitions(BestSpeechDiagnostics PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET BestSpeechDiagnostics PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    file(GLOB BESTSPEECH_ENGINE_DLLS
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/b32_tts.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll_*.dll"
    )

    # The 32-bit shim that drives both engine families, and the standalone helper the
    # verification tooling uses.
    set(BESTSPEECH_RUNTIME_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/b32_wrapper.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/b32_helper.exe"
    )

    add_custom_command(TARGET BestspeechSAPI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${BESTSPEECH_ENGINE_DLLS} ${BESTSPEECH_RUNTIME_FILES}
            "$<TARGET_FILE_DIR:BestspeechSAPI>"
        COMMENT "Copying engine dlls and shim to output directory"
    )

    install(TARGETS BestspeechSAPI BestspeechServer
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .
    )

    install(FILES ${BESTSPEECH_ENGINE_DLLS} ${BESTSPEECH_RUNTIME_FILES}
        DESTINATION .
    )

    install(FILES "${CMAKE_BINARY_DIR}/bin/Release/x64/BestspeechSAPI.dll"
        DESTINATION x64
        OPTIONAL
    )

else()
    add_library(pipe_client STATIC
        src/pipe_client.cpp
    )

    target_include_directories(pipe_client PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(pipe_client PUBLIC
        shlwapi
    )

    target_compile_definitions(pipe_client PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    add_library(BestspeechSAPI SHARED
        src/sapi_main.cpp
        src/com.cpp
        src/registry.cpp
        src/ISpDataKeyImpl.cpp
        src/IEnumSpObjectTokensImpl.cpp
        src/ISpTTSEngineImpl.cpp
        src/helper_client.cpp
        src/text_pipeline.cpp
        src/voice_token.cpp
        src/bestspeech_sapi.def
    )

    target_include_directories(BestspeechSAPI PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(BestspeechSAPI PRIVATE
        pipe_client
        ole32
        oleaut32
        advapi32
        shell32
    )

    target_compile_definitions(BestspeechSAPI PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
        BUILD_X64
    )

    if(MSVC)
        target_compile_options(pipe_client PRIVATE /W3 /MP)
        target_compile_options(BestspeechSAPI PRIVATE /W3 /MP)

        set_property(TARGET pipe_client PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_property(TARGET BestspeechSAPI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_executable(sapi_probe tools/sapi_probe.cpp)

    target_include_directories(sapi_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(sapi_probe PRIVATE ole32 oleaut32)

    target_compile_definitions(sapi_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET sapi_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Shipped with the product: a user-runnable check that walks every installed voice
    # through SAPI and writes a plain text report, for when a voice will not speak.
    # Hammers the engine with utterances the host abandons part way through, which is
    # what a screen reader does constantly. A cancel that leaves the helper protocol out
    # of step shows up here as a hang or as silence on the utterances that follow.
    add_executable(cancel_probe tools/cancel_probe.cpp)

    target_include_directories(cancel_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(cancel_probe PRIVATE ole32 oleaut32)

    target_compile_definitions(cancel_probe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET cancel_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_executable(BestSpeechDiagnostics
        tools/bestspeech_diag.cpp
        src/registry.cpp
    )

    target_include_directories(BestSpeechDiagnostics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(BestSpeechDiagnostics PRIVATE ole32 oleaut32 advapi32 sapi)

    target_compile_definitions(BestSpeechDiagnostics PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        set_property(TARGET BestSpeechDiagnostics PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    install(TARGETS BestspeechSAPI
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .
    )
endif()

# Packaging is handled by build_all.bat, which stages output/ and compiles
# installer/BestspeechSAPI.iss with Inno Setup directly. CPack's generated script could
# not express registering two COM servers into two registry views, or stopping the
# worker process before replacing files it holds open.
