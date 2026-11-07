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
)

target_include_directories(LoopTexturerTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(LoopTexturerTests PRIVATE hadronyche_testhost)
add_dependencies(LoopTexturerTests LoopTexturer)

# It drives a real window with Windows' mouse messages.
if(WIN32)
    target_sources(LoopTexturerTests PRIVATE PanelTest.cpp)
endif()

set(LOOPTEXTURER_TESTS
    class-ids parameter-ids parameter-ranges parameter-labels saved-format older-project older-length older-tape-replaced
    latency gain-volume pan lfo filter saturator aliasing broken-input extreme-settings parameter-state parameter-text
    free-playback free-exact-timing free-length-repitches free-each-length overdub
    feedback-unity feedback-below-unity feedback-above-unity feedback-keeps-low-end dry-wet clear rec-moves-to-overdub free-stop-play
    sync-grid sync-lengths sync-open-ended sync-64-bars sync-transport sync-tempo-repitches sync-seam sync-overdub sync-locate-cancels saved-loops
    panel panel-opens
)

hadronyche_add_host_tests(LoopTexturerTests LoopTexturer ${LOOPTEXTURER_TESTS})

hadronyche_add_lv2_tests(LoopTexturer)

# The LV2's looper through its panel, with the messages carried as a host carries them. Linux only, like the LV2.
if(LINUX)
    add_executable(LoopTexturerLv2Looper lv2/LooperLv2Test.cpp)
    target_link_libraries(LoopTexturerLv2Looper PRIVATE xcb ${CMAKE_DL_LIBS})
    add_dependencies(LoopTexturerLv2Looper LoopTexturer-lv2)
    add_dependencies(LoopTexturerLv2Compare LoopTexturerLv2Looper)
    add_test(NAME lv2-looper COMMAND xvfb-run -a -s "-screen 0 2000x600x24"
        "$<TARGET_FILE:LoopTexturerLv2Looper>" "${CMAKE_BINARY_DIR}/LV2/LoopTexturer.lv2")
    set_tests_properties(lv2-looper PROPERTIES TIMEOUT 300)
endif()
