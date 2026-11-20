cmake_minimum_required(VERSION 4.0)
if(NOT DEFOLD_SOURCE OR NOT DEFOLD_SDK_ROOT)
    message(FATAL_ERROR "Set DEFOLD_SOURCE to the DataStore/event branch and DEFOLD_SDK_ROOT to its prepared SDK")
endif()
set(DEFOLD_BUILD_HOME "${CMAKE_BINARY_DIR}/defold-build" CACHE PATH "Isolated engine outputs")
set(DEFOLD_SKIP_BOB_LIGHT ON CACHE BOOL "Use prepared Bob" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build the sample tests separately" FORCE)
list(APPEND CMAKE_MODULE_PATH "${DEFOLD_SOURCE}/scripts/cmake")
include(defold)
project(MoonBase LANGUAGES ${DEFOLD_LANGUAGE_LIST})
add_subdirectory("${DEFOLD_SOURCE}" "${CMAKE_BINARY_DIR}/defold")
option(MOONBASE_OFFSCREEN "Keep the validation engine window hidden" OFF)
if(MOONBASE_OFFSCREEN)
    # The current engine always shows its window during bootstrap. Patch only an
    # isolated build copy for capture runs; the engine checkout is left untouched.
    set(MOONBASE_ENGINE_SOURCE "${DEFOLD_SOURCE}/engine/engine/src/engine.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MOONBASE_ENGINE_SOURCE}")
    file(READ "${MOONBASE_ENGINE_SOURCE}" MOONBASE_HIDDEN_SOURCE)
    string(REPLACE "window_params.m_Title                   ="
        "window_params.m_Hidden = true;\n        window_params.m_Title                   ="
        MOONBASE_HIDDEN_SOURCE "${MOONBASE_HIDDEN_SOURCE}")
    string(REPLACE "dmPlatform::ShowWindow(engine->m_Window);" "/* Hidden capture engine. */"
        MOONBASE_HIDDEN_SOURCE "${MOONBASE_HIDDEN_SOURCE}")
    if(NOT MOONBASE_HIDDEN_SOURCE MATCHES "window_params[.]m_Hidden = true;")
        message(FATAL_ERROR "This checkout needs an updated hidden-window bootstrap patch")
    endif()
    set(MOONBASE_HIDDEN_CPP "${CMAKE_BINARY_DIR}/moonbase_hidden_engine.cpp")
    file(WRITE "${MOONBASE_HIDDEN_CPP}" "${MOONBASE_HIDDEN_SOURCE}")
    get_target_property(MOONBASE_ENGINE_SOURCES engine SOURCES)
    list(REMOVE_ITEM MOONBASE_ENGINE_SOURCES "${MOONBASE_ENGINE_SOURCE}")
    set_property(TARGET engine PROPERTY SOURCES ${MOONBASE_ENGINE_SOURCES} "${MOONBASE_HIDDEN_CPP}")
endif()
set(MOONBASE_SOURCES moonbase/src/simulation.cpp moonbase/src/geometry.cpp moonbase/src/moonbase.cpp)
# Compile into the normal engine target to inherit its exact SDK and link configuration.
target_sources(dmengine PRIVATE ${MOONBASE_SOURCES})
target_include_directories(dmengine BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/moonbase/include"
    "${DEFOLD_SOURCE}/engine/engine/src"
    "${DEFOLD_SOURCE}/engine/extension/src"
    "${DEFOLD_SOURCE}/engine/input/src"
    "${DEFOLD_SOURCE}/engine/hid/src"
    "${DEFOLD_SOURCE}/engine/physics/src"
    "${DEFOLD_SOURCE}/engine/sdk/src")
target_link_libraries(dmengine PRIVATE datastore)
if(TARGET_PLATFORM STREQUAL "wasm-web")
    # The SDK's scalar vectormath indexes adjacent members as arrays. Recent LLVM
    # needs this to preserve those accesses (also used by the engine's Android build).
    target_compile_options(defold_sdk INTERFACE -fno-strict-aliasing)
    # Browser archives use the virtual filesystem, rather than the Node test mount.
    get_target_property(MOONBASE_WEB_DEFINES defold_sdk INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER MOONBASE_WEB_DEFINES EXCLUDE REGEX "^DM_HOSTFS=")
    set_property(TARGET defold_sdk PROPERTY INTERFACE_COMPILE_DEFINITIONS ${MOONBASE_WEB_DEFINES})
    if(WITH_WEBGPU)
        # Emscripten 6 replaces USE_WEBGPU with Dawn's current C bindings.
        get_target_property(MOONBASE_WEBGPU_LINK_OPTIONS defold_sdk INTERFACE_LINK_OPTIONS)
        list(FILTER MOONBASE_WEBGPU_LINK_OPTIONS EXCLUDE REGEX "^-sUSE_WEBGPU")
        set_property(TARGET defold_sdk PROPERTY INTERFACE_LINK_OPTIONS ${MOONBASE_WEBGPU_LINK_OPTIONS})
        target_compile_definitions(graphics_webgpu PRIVATE DM_GRAPHICS_WEBGPU2 DM_GRAPHICS_DAWN)
        target_compile_options(graphics_webgpu PRIVATE --use-port=emdawnwebgpu)
        target_link_options(dmengine PRIVATE --use-port=emdawnwebgpu)
        # This Dawn port serializes the clear value even for depth loads. Supply
        # a finite ignored value instead of the native API's NaN sentinel.
        set(MOONBASE_WEBGPU_SOURCE "${DEFOLD_SOURCE}/engine/graphics/src/webgpu/graphics_webgpu.cpp")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MOONBASE_WEBGPU_SOURCE}")
        file(READ "${MOONBASE_WEBGPU_SOURCE}" MOONBASE_WEBGPU_TEXT)
        string(REPLACE
            "WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;"
            "WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;\n    dsAttachment.depthClearValue = 1.0f;"
            MOONBASE_WEBGPU_TEXT "${MOONBASE_WEBGPU_TEXT}")
        set(MOONBASE_WEBGPU_CPP "${CMAKE_BINARY_DIR}/moonbase_webgpu.cpp")
        file(WRITE "${MOONBASE_WEBGPU_CPP}" "${MOONBASE_WEBGPU_TEXT}")
        get_target_property(MOONBASE_WEBGPU_SOURCES graphics_webgpu SOURCES)
        list(REMOVE_ITEM MOONBASE_WEBGPU_SOURCES "${MOONBASE_WEBGPU_SOURCE}")
        set_property(TARGET graphics_webgpu PROPERTY SOURCES ${MOONBASE_WEBGPU_SOURCES} "${MOONBASE_WEBGPU_CPP}")
        target_include_directories(graphics_webgpu PRIVATE "${DEFOLD_SOURCE}/engine/graphics/src/webgpu")
    endif()
    # Recent Emscripten releases require a newer browser baseline.
    get_target_property(MOONBASE_WEB_LINK_OPTIONS defold_sdk INTERFACE_LINK_OPTIONS)
    list(FILTER MOONBASE_WEB_LINK_OPTIONS EXCLUDE REGEX "^-sMIN_(FIREFOX|SAFARI|CHROME)_VERSION=")
    set_property(TARGET defold_sdk PROPERTY INTERFACE_LINK_OPTIONS ${MOONBASE_WEB_LINK_OPTIONS})
    target_link_options(dmengine PRIVATE
        -sMIN_FIREFOX_VERSION=128 -sMIN_SAFARI_VERSION=180000 -sMIN_CHROME_VERSION=120)
endif()
get_target_property(MOONBASE_ENGINE_DIR dmengine BINARY_DIR)
set(MOONBASE_EXPORTS "${MOONBASE_ENGINE_DIR}/__exported_symbols_dmengine.cpp")
file(READ "${MOONBASE_EXPORTS}" MOONBASE_EXPORT_TEXT)
string(REPLACE "extern \"C\" void dmExportedSymbols() {"
    "extern \"C\" void MoonBase();\nextern \"C\" void dmExportedSymbols() {\n    MoonBase();"
    MOONBASE_EXPORT_TEXT "${MOONBASE_EXPORT_TEXT}")
file(WRITE "${MOONBASE_EXPORTS}" "${MOONBASE_EXPORT_TEXT}")
add_executable(moonbase_tests tests/test_simulation.cpp moonbase/src/simulation.cpp)
target_link_libraries(moonbase_tests PRIVATE datastore dlib profile_null)
target_compile_definitions(moonbase_tests PRIVATE DLIB_LOG_DOMAIN="MOONBASE_TESTS")
target_include_directories(moonbase_tests PRIVATE moonbase/src)
