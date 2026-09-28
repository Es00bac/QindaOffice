# SPDX-License-Identifier: GPL-3.0-or-later

# office_speech (D-057, docs/live-notes.md): live audio into text for the
# suite. PipeWire capture through pw-record (microphone, the desktop's
# output monitor, or both mixed), an energy segmenter, and three
# recognizers behind one interface — ElevenLabs realtime (WebSocket),
# local faster-whisper through the qindaoffice-whisper helper, and any
# OpenAI-compatible /audio/transcriptions endpoint. Qt Core/Network/
# WebSockets only: no Gui, no Quick, no desktop archives, so every test is
# a cheap offscreen one. Keys are handed in by the caller; this library
# never touches the keyring.
find_package(Qt6 REQUIRED COMPONENTS WebSockets)

qt_add_library(office_speech STATIC
    src/elevenlabs_realtime.cpp
    src/elevenlabs_realtime.h
    src/http_transcription.cpp
    src/http_transcription.h
    src/pcm_capture.cpp
    src/pcm_capture.h
    src/pcm_mixer.cpp
    src/pcm_mixer.h
    src/speech_recognizer.cpp
    src/speech_recognizer.h
    src/speech_segmenter.cpp
    src/speech_segmenter.h
    src/whisper_local.cpp
    src/whisper_local.h
)

target_include_directories(office_speech
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# AGENT-CONTRACT: the helper's installed path (tools/qindaoffice-whisper,
# installed below) is compiled in; QQO_WHISPER_HELPER overrides it for the
# tests and for a build-tree run.
include(GNUInstallDirs)
target_compile_definitions(office_speech PRIVATE
    QQO_WHISPER_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/qindaoffice/qindaoffice-whisper"
    QQO_WHISPER_HELPER_SOURCE="${PROJECT_SOURCE_DIR}/tools/qindaoffice-whisper")

target_link_libraries(office_speech
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
)

qqo_set_strict_warnings(office_speech)

install(PROGRAMS ${PROJECT_SOURCE_DIR}/tools/qindaoffice-whisper
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/qindaoffice)
