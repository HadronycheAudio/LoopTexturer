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
    "${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/ustring.cpp"
    "${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp"
)

target_include_directories(LoopTexturerTests PRIVATE "${VST3_SDK_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(LoopTexturerTests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(LoopTexturerTests PRIVATE hadronyche_params user32 gdi32)
add_dependencies(LoopTexturerTests LoopTexturer)

# One CTest test per test in the program, so that a single one can be run with ctest -R.
set(LOOPTEXTURER_TESTS
    class-ids parameter-ids parameter-ranges parameter-labels saved-format older-project older-length older-tape-replaced
    latency gain-volume pan lfo filter saturator aliasing extreme-settings parameter-state parameter-text
    free-playback free-exact-timing free-length-repitches free-each-length overdub
    feedback-unity feedback-below-unity feedback-above-unity feedback-keeps-low-end dry-wet clear rec-moves-to-overdub free-stop-play
    sync-grid sync-lengths sync-open-ended sync-64-bars sync-transport sync-tempo-repitches sync-seam sync-overdub sync-locate-cancels saved-loops
    panel
)

foreach(test_name ${LOOPTEXTURER_TESTS})
    add_test(NAME ${test_name} COMMAND LoopTexturerTests "$<TARGET_FILE:LoopTexturer>" ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
endforeach()
