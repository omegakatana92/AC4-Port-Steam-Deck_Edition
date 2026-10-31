# Tests use synthetic fixtures only: no game data, no game-derived code.
add_executable(ac4-tests
  harness.cpp
  test_bytes.cpp
  test_sha.cpp
  test_ini.cpp
  test_settings.cpp
  test_sfo.cpp
  test_privacy.cpp
  test_prep.cpp
  test_decode.cpp
  test_recomp.cpp
  test_spu_pipeline.cpp)
target_link_libraries(ac4-tests PRIVATE ac4_recomp ac4_prep ac4_ppu ac4_spu)
target_compile_options(ac4-tests PRIVATE -Wall -Wextra)

add_test(NAME ac4-tests
         COMMAND ac4-tests
                 --recomp $<TARGET_FILE:ac4-recomp>
                 --prep $<TARGET_FILE:ac4-prep>
                 --spurecomp $<TARGET_FILE:ac4-spurecomp>)
