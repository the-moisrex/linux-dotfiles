add_executable(test_chaining test_chaining.cpp)
target_link_libraries(test_chaining PRIVATE prompt_lib)
target_include_directories(test_chaining PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME chaining COMMAND test_chaining)

add_executable(test_errors test_errors.cpp)
target_link_libraries(test_errors PRIVATE prompt_lib)
target_include_directories(test_errors PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME errors COMMAND test_errors)

add_executable(test_fs test_fs.cpp)
target_link_libraries(test_fs PRIVATE prompt_lib)
target_include_directories(test_fs PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME fs COMMAND test_fs)

add_executable(test_sanitize test_sanitize.cpp)
target_link_libraries(test_sanitize PRIVATE prompt_lib)
target_include_directories(test_sanitize PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sanitize COMMAND test_sanitize)

add_executable(test_embed test_embed.cpp)
target_link_libraries(test_embed PRIVATE prompt_lib)
target_include_directories(test_embed PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME embed COMMAND test_embed)

add_executable(test_completion test_completion.cpp)
target_link_libraries(test_completion PRIVATE prompt_lib)
target_include_directories(test_completion PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME completion COMMAND test_completion)

# End-to-end smoke: the built binary numbers the lines of an embedded file when
# asked (and only then).
add_test(NAME files_num
    COMMAND $<TARGET_FILE:prompt> files --num ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/numbered.txt)
set_tests_properties(files_num PROPERTIES PASS_REGULAR_EXPRESSION "1 \\| one")

# End-to-end smoke: `--complete` answers from the prompt's own help (flags) and
# its own code (flag values) without running the prompt or the network.
add_test(NAME complete_flags COMMAND $<TARGET_FILE:prompt> metadata --complete --)
set_tests_properties(complete_flags PROPERTIES PASS_REGULAR_EXPRESSION "--format")

add_test(NAME complete_values COMMAND $<TARGET_FILE:prompt> diagram --complete "" --type)
set_tests_properties(complete_values PROPERTIES PASS_REGULAR_EXPRESSION "mermaid")

# End-to-end smoke: `tse.analyze` is instructions only — it takes no arguments,
# fetches nothing and needs no bin/ tool, so it can run without the network.
add_test(NAME tse_analyze_instructions COMMAND $<TARGET_FILE:prompt> tse.analyze)
set_tests_properties(tse_analyze_instructions PROPERTIES PASS_REGULAR_EXPRESSION "institutional analyst")
