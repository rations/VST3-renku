# jdbackend.cmake — the audio backends shared with JackDAW (haiku/jdbackend/).
#
# Defines, once per build:
#   jdbackend       static library: the Media Kit backend (built in) and the
#                   loader that finds the JACK add-on (audio_backend_load_for).
#                   Links libmedia/libmidi2/libbe, never libjack.
#   jdbackend_jack  the JACK backend as an add-on, jack.so, in
#                   <build>/bin/<config>/backends/ (where the loader looks next
#                   to an executable). Only when libjack is found.
#
# Included by VST3-renku's top-level CMakeLists.txt before the SDK (whose
# audiohost sample looks for jdbackend) and by projects that build against
# this SDK on their own, such as NAMku.

include_guard(GLOBAL)

if(NOT HAIKU OR TARGET jdbackend)
    return()
endif()

set(JDBACKEND_DIR "${CMAKE_CURRENT_LIST_DIR}/jdbackend")

add_library(jdbackend STATIC
    ${JDBACKEND_DIR}/backend/mediakit/mediakit_backend.cpp
    ${JDBACKEND_DIR}/engine/audio_backend.c
    ${JDBACKEND_DIR}/engine/rt_ringbuffer.c
)
target_include_directories(jdbackend PUBLIC ${JDBACKEND_DIR})
target_compile_features(jdbackend PRIVATE cxx_std_17)
target_compile_options(jdbackend PRIVATE -Wall -Wextra)
set_target_properties(jdbackend PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(jdbackend PUBLIC media midi2 be)

# libjack only, not libjackserver: the add-on is a JACK client.
find_path(JDBACKEND_JACK_INCLUDE_DIR jack/jack.h)
find_library(JDBACKEND_JACK_LIBRARY NAMES jack libjack)
if(JDBACKEND_JACK_INCLUDE_DIR AND JDBACKEND_JACK_LIBRARY)
    add_library(jdbackend_jack MODULE ${JDBACKEND_DIR}/backend/jack/jack_backend.c)
    target_include_directories(jdbackend_jack PRIVATE ${JDBACKEND_DIR}
                                                      ${JDBACKEND_JACK_INCLUDE_DIR})
    target_compile_options(jdbackend_jack PRIVATE -Wall -Wextra)
    target_link_libraries(jdbackend_jack PRIVATE ${JDBACKEND_JACK_LIBRARY})
    set_target_properties(jdbackend_jack
        PROPERTIES
            PREFIX ""
            OUTPUT_NAME jack
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/backends
    )
else()
    message(STATUS "[jdbackend] JACK not found - no jack.so add-on (the Media Kit still works)")
endif()
