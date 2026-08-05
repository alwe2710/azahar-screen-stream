add_executable(tests
    common/bit_field.cpp
    common/file_util.cpp
    common/param_package.cpp
    core/core_timing.cpp
    core/file_sys/path_parser.cpp
    core/hle/kernel/hle_ipc.cpp
    core/memory/memory.cpp
    core/memory/vm_manager.cpp
    core/streaming/handshake_messages.cpp
    precompiled_headers.h
    audio_core/hle/hle.cpp
    audio_core/hle/source.cpp
    audio_core/lle/lle.cpp
    audio_core/audio_fixures.h
    audio_core/decoder_tests.cpp
    video_core/shader.cpp
    audio_core/merryhime_3ds_audio/merry_audio/merry_audio.cpp
    audio_core/merryhime_3ds_audio/merry_audio/merry_audio.h
    audio_core/merryhime_3ds_audio/merry_audio/service_fixture.cpp
    audio_core/merryhime_3ds_audio/merry_audio/service_fixture.h
    audio_core/merryhime_3ds_audio/audio_test_biquad_filter.cpp
)

create_target_directory_groups(tests)

if (BSD STREQUAL "NetBSD")
    include(DisablePaxMprotect)
    disable_pax_mprotect(tests)
endif()

# unison_core added directly (not just transitively via citra_core, which
# links it PRIVATE -- that doesn't propagate its include directory to
# this target) so tests/core/streaming/handshake_messages.cpp can include
# unison/handshake.h itself, to round-trip through the real shared
# parser rather than only substring-matching JSON output.
#
# Deliberately still the project's own (stale) externals/unison here, not
# a pointed-elsewhere override: a first attempt at that (pointing this
# file's #include at the current unison/core sibling checkout while still
# linking unison_core's already-compiled, stale-ABI library) looked like
# it worked -- it compiled -- but silently read garbage/zeroed memory for
# the one field (session_ready.video_mode) the two disagreed about, since
# the compiled library's own sizeof(unison_session_ready)/memset() still
# used the smaller, field-missing layout. That's a real ODR/ABI violation,
# not a shortcut -- test_unison_messages.cpp's own comment covers what's
# actually still tested via plain JSON substring checks as a result.
target_link_libraries(tests PRIVATE citra_common citra_core video_core audio_core unison_core)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} catch2 nihstro-headers Threads::Threads)

if (ENABLE_LIBRETRO)
    target_link_libraries(tests PRIVATE $<TARGET_OBJECTS:citra_libretro_common>)
endif()

add_test(NAME tests COMMAND tests)
if(NOT ANDROID AND (CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME))
    catch_discover_tests(tests)
endif()


if (CITRA_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(tests PRIVATE precompiled_headers.h)
endif()

# Bundle in-place on MSVC so dependencies can be resolved by builds.
if (MSVC)
    include(BundleTarget)
    bundle_target_in_place(tests)
endif()
