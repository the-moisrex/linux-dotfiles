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

add_executable(test_sink test_sink.cpp)
target_link_libraries(test_sink PRIVATE prompt_lib)
target_include_directories(test_sink PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sink COMMAND test_sink)

add_test(NAME ai_help COMMAND $<TARGET_FILE:prompt> .ai.dseek --help)
set_tests_properties(ai_help PROPERTIES PASS_REGULAR_EXPRESSION "--with-context")

add_test(NAME ai_help_tty COMMAND $<TARGET_FILE:prompt> .ai.dseek --help)
set_tests_properties(ai_help_tty PROPERTIES PASS_REGULAR_EXPRESSION "--tty")

add_test(NAME ai_print_prompt COMMAND $<TARGET_FILE:prompt> .note hello .ai.dseek --print-prompt)
set_tests_properties(ai_print_prompt PROPERTIES PASS_REGULAR_EXPRESSION "hello")

add_test(NAME ai_head COMMAND $<TARGET_FILE:prompt> .note one .note two .ai.dseek --print-prompt --head 1)
set_tests_properties(ai_head PROPERTIES PASS_REGULAR_EXPRESSION "one" FAIL_REGULAR_EXPRESSION "two")

# End-to-end `.ai.*` sinks: stub providers prove --tty/--width/--height are
# forwarded on a terminal, the styled stream stays display-only and the chain
# gets the plain --context-file answer.
add_test(NAME ai_sink_roundtrip
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_ai_sink.sh $<TARGET_FILE:prompt>)

# End-to-end `.save`: explicit/directory/derived targets, passthrough into a
# sink, --head, no-clobber, sanitized names and the empty-context guard.
add_test(NAME save_roundtrip
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_save.sh
            $<TARGET_FILE:prompt> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/numbered.txt)

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

# End-to-end smoke: `length` turns a target into an answer-length instruction,
# standalone and chained, with the exact/approximate and unit spellings.
add_test(NAME length_exact COMMAND $<TARGET_FILE:prompt> .length =200)
set_tests_properties(length_exact PROPERTIES PASS_REGULAR_EXPRESSION "exactly 200 words")

add_test(NAME length_units COMMAND $<TARGET_FILE:prompt> length 10 lines)
set_tests_properties(length_units PROPERTIES PASS_REGULAR_EXPRESSION "about 10 lines")

add_test(NAME length_preset_chain COMMAND $<TARGET_FILE:prompt> .note hello .length short)
set_tests_properties(length_preset_chain PROPERTIES PASS_REGULAR_EXPRESSION "Keep your answer short")

add_test(NAME length_default COMMAND $<TARGET_FILE:prompt> length)
set_tests_properties(length_default PROPERTIES PASS_REGULAR_EXPRESSION "one or two paragraphs")

add_test(NAME length_shape COMMAND $<TARGET_FILE:prompt> .length blog)
set_tests_properties(length_shape PROPERTIES PASS_REGULAR_EXPRESSION "blog post")

add_test(NAME length_conflict COMMAND $<TARGET_FILE:prompt> length --max --min 5)
set_tests_properties(length_conflict PROPERTIES WILL_FAIL TRUE)
