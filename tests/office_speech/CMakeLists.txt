# SPDX-License-Identifier: GPL-3.0-or-later
# office_speech (D-057): the mixer and segmenter on synthetic audio, the
# capture through a fake pw-record, ElevenLabs realtime against a local
# WebSocket server, local Whisper through a fake helper (and the real
# helper's refusal paths), and HTTP transcription against a local server.
find_package(Qt6 REQUIRED COMPONENTS WebSockets)
add_executable(tst_office_speech tst_office_speech.cpp)
target_link_libraries(tst_office_speech PRIVATE office_speech Qt6::Test Qt6::WebSockets)
target_compile_definitions(tst_office_speech PRIVATE
    QQO_WHISPER_HELPER_REAL="${PROJECT_SOURCE_DIR}/tools/qindaoffice-whisper")
qqo_set_strict_warnings(tst_office_speech)
add_test(NAME tst_office_speech COMMAND tst_office_speech)
set_tests_properties(tst_office_speech PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
