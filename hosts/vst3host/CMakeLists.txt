# vst3host / vst3jackhost — headless VST3 host with a stdin parameter REPL and
# the plug-in's native editor window. Reuses the SDK audiohost sample's
# AudioClient glue with one of its media servers:
#   vst3host      the native Media Kit (always built on Haiku)
#   vst3jackhost  JACK (built when JACK is found)

set(SDK_DIR ${CMAKE_SOURCE_DIR}/vst3sdk)
set(AUDIOHOST_DIR ${SDK_DIR}/public.sdk/samples/vst-hosting/audiohost)

list(APPEND CMAKE_MODULE_PATH ${SDK_DIR}/cmake/modules)
include(SMTG_FindJack)

set(vst3host_common_sources
    source/main.cpp
    source/editorwindow.cpp
    ${AUDIOHOST_DIR}/source/media/audioclient.cpp
    ${SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${SDK_DIR}/public.sdk/source/vst/hosting/module_haiku.cpp
    ${SDK_DIR}/public.sdk/source/common/memorystream.cpp
)

function(vst3host_target target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_include_directories(${target} PRIVATE ${SDK_DIR})
    target_link_libraries(${target} PRIVATE sdk_hosting be)
    set_target_properties(${target}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>
    )
endfunction()

# Media Kit
add_executable(vst3host
    ${vst3host_common_sources}
    ${AUDIOHOST_DIR}/source/media/mediakit/mediakitclient.cpp
)
vst3host_target(vst3host)
target_compile_definitions(vst3host PRIVATE VST3HOST_MEDIAKIT)
target_link_libraries(vst3host PRIVATE jdbackend_mediakit)

# JACK
if(NOT ${LIBJACK_FOUND})
    message(STATUS "[VST3-renku] JACK not found - skipping vst3jackhost.")
    return()
endif()

add_executable(vst3jackhost
    ${vst3host_common_sources}
    ${AUDIOHOST_DIR}/source/media/jack/jackclient.cpp
)
vst3host_target(vst3jackhost)
target_include_directories(vst3jackhost PRIVATE ${LIBJACK_INCLUDE_DIR})
target_link_libraries(vst3jackhost PRIVATE ${LIBJACK_LIBRARY})
