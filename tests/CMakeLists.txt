# A test program that loads the built plug-in and drives it like a host. It doesn't link any of the
# plug-in's code: it only uses a few of its headers, for the parameter IDs and looper commands.

add_executable(LoopTexturerTests
    TestMain.cpp
    Tests.h
    Harness.h
    Harness.cpp
    CompatibilityTests.cpp
    ChainTests.cpp
    LooperTests.cpp
    PanelTest.cpp
)

target_include_directories(LoopTexturerTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(LoopTexturerTests PRIVATE hadronyche_testhost)
add_dependencies(LoopTexturerTests LoopTexturer)

set(LOOPTEXTURER_TESTS
    class-ids parameter-ids parameter-ranges parameter-labels saved-format older-project older-length older-tape-replaced
    latency gain-volume pan lfo filter saturator aliasing extreme-settings parameter-state parameter-text
    free-playback free-exact-timing free-length-repitches free-each-length overdub
    feedback-unity feedback-below-unity feedback-above-unity feedback-keeps-low-end dry-wet clear rec-moves-to-overdub free-stop-play
    sync-grid sync-lengths sync-open-ended sync-64-bars sync-transport sync-tempo-repitches sync-seam sync-overdub sync-locate-cancels saved-loops
    panel
)

hadronyche_add_host_tests(LoopTexturerTests LoopTexturer ${LOOPTEXTURER_TESTS})
