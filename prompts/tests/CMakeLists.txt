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

add_executable(test_facts test_facts.cpp)
target_link_libraries(test_facts PRIVATE prompt_lib)
target_include_directories(test_facts PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME facts COMMAND test_facts)

# The pasted-text → instrument policy, now next to the clipboard instead of
# inline in the dispatcher's environment initializer.
add_executable(test_clipboard test_clipboard.cpp)
target_link_libraries(test_clipboard PRIVATE prompt_lib)
target_include_directories(test_clipboard PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME clipboard_identifier COMMAND test_clipboard)

add_test(NAME ai_help COMMAND $<TARGET_FILE:prompt> .ai.dseek --help)
set_tests_properties(ai_help PROPERTIES PASS_REGULAR_EXPRESSION "--with-context")

add_test(NAME ai_debate_help COMMAND $<TARGET_FILE:prompt> .ai.debate --help)
set_tests_properties(ai_debate_help PROPERTIES PASS_REGULAR_EXPRESSION "--rounds")

add_test(NAME ai_debate_help_state COMMAND $<TARGET_FILE:prompt> .ai.debate --help)
set_tests_properties(ai_debate_help_state PROPERTIES PASS_REGULAR_EXPRESSION "--continue")

add_test(NAME ai_debate_print_prompt
    COMMAND $<TARGET_FILE:prompt> .note hello .ai.debate grok dseek --print-prompt)
set_tests_properties(ai_debate_print_prompt PROPERTIES PASS_REGULAR_EXPRESSION "Another AI said")

# The completion callback offers the provider names, and PROVIDER= after
# -m/--model.
add_test(NAME ai_debate_complete
    COMMAND $<TARGET_FILE:prompt> .ai.debate --complete gro)
set_tests_properties(ai_debate_complete PROPERTIES PASS_REGULAR_EXPRESSION "grok")

add_test(NAME ai_debate_complete_model
    COMMAND $<TARGET_FILE:prompt> .ai.debate --complete "" -m)
set_tests_properties(ai_debate_complete_model PROPERTIES PASS_REGULAR_EXPRESSION "deepseek=")

# The relay loop itself, with fake conversations (no network, no browser).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME ai_debate_unit
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ai_debate.py)
endif()

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

# End-to-end `.ai.debate`: the same side-channel plumbing as `.ai.*`, plus the
# forwarded provider names, the debate flags and stdin.
add_test(NAME ai_debate_sink
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_ai_debate_sink.sh $<TARGET_FILE:prompt>)

# End-to-end `.save`: explicit/directory/derived targets, --dir/--file,
# preflight fail-fast, passthrough into a sink, --head, no-clobber, sanitized
# names and the empty-context guard.
add_test(NAME save_roundtrip
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_save.sh
            $<TARGET_FILE:prompt> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/numbered.txt)

# End-to-end `.clear`/`.flush`: a context reset between sections, piped stdin,
# hint/taint reset, `.flush` == `.save .clear`, no-clobber and preflight.
add_test(NAME clear_roundtrip
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_clear.sh $<TARGET_FILE:prompt>)

# `tse_data::run_tool`: a timeout, a non-zero exit and an empty dataset each
# produce a message instead of the old silent empty soft_error.
add_executable(test_tse_tool test_tse_tool.cpp)
target_link_libraries(test_tse_tool PRIVATE prompt_lib)
target_include_directories(test_tse_tool PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tse_tool COMMAND test_tse_tool)

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

# End-to-end smoke: the historical base-rate lens is instructions only too.
add_test(NAME tse_analyze_history_instructions COMMAND $<TARGET_FILE:prompt> tse.analyze.history)
set_tests_properties(tse_analyze_history_instructions PROPERTIES PASS_REGULAR_EXPRESSION "mid-trend correction")

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

# The agentic layer: the response contract, extraction, command printing, loop
# item listing/printing, and one real parallel loop. No network.
add_test(NAME agent_expect_contract COMMAND $<TARGET_FILE:prompt> .expect command:prompt)
set_tests_properties(agent_expect_contract PROPERTIES PASS_REGULAR_EXPRESSION "single `prompt` command")

add_test(NAME agent_extract
    COMMAND $<TARGET_FILE:prompt> .note [=[```sh
prompt .length 5
```]=] .expect command:prompt .extract)
set_tests_properties(agent_extract PROPERTIES PASS_REGULAR_EXPRESSION "prompt \\.length 5")

add_test(NAME agent_exec_print COMMAND $<TARGET_FILE:prompt> .exec --print git status)
set_tests_properties(agent_exec_print PROPERTIES PASS_REGULAR_EXPRESSION "git status")

add_test(NAME agent_exec_args COMMAND $<TARGET_FILE:prompt> .exec --print -- echo "a b")
set_tests_properties(agent_exec_args PROPERTIES PASS_REGULAR_EXPRESSION "echo 'a b'")

# `--` hands the rest of argv to the command: its own `-h` is not prompt's.
add_test(NAME agent_exec_help_arg COMMAND $<TARGET_FILE:prompt> .exec --print -- echo -h)
set_tests_properties(agent_exec_help_arg PROPERTIES PASS_REGULAR_EXPRESSION "echo -h")

add_test(NAME agent_exec_chain_print COMMAND $<TARGET_FILE:prompt> .exec --print -- prompt .note hi)
set_tests_properties(agent_exec_chain_print PROPERTIES PASS_REGULAR_EXPRESSION "prompt \\.note hi")

add_test(NAME agent_exec_chain_run COMMAND $<TARGET_FILE:prompt> .exec --yes -- prompt .note hi)
set_tests_properties(agent_exec_chain_run PROPERTIES PASS_REGULAR_EXPRESSION "hi")

add_test(NAME agent_exec_allowed
    COMMAND ${CMAKE_COMMAND} -E env PROMPT_POLICY_ALLOW=echo $<TARGET_FILE:prompt> .exec echo hi)
set_tests_properties(agent_exec_allowed PROPERTIES PASS_REGULAR_EXPRESSION "hi")

add_test(NAME agent_exec_denied COMMAND $<TARGET_FILE:prompt> .exec sudo ls)
set_tests_properties(agent_exec_denied PROPERTIES WILL_FAIL TRUE)

add_test(NAME agent_each_print
    COMMAND $<TARGET_FILE:prompt> .note [=[| symbol |
| --- |
| a |
| b |]=] .each --print .tse {symbol} .end)
set_tests_properties(agent_each_print PROPERTIES PASS_REGULAR_EXPRESSION "prompt .tse b")

add_test(NAME agent_each_run
    COMMAND $<TARGET_FILE:prompt> .note [=[| symbol |
| --- |
| a |
| b |]=] .each .length 10 .end)
set_tests_properties(agent_each_run PROPERTIES PASS_REGULAR_EXPRESSION "## b")

add_test(NAME agent_each_list
    COMMAND $<TARGET_FILE:prompt> .note [=[| symbol |
| --- |
| a |
| b |]=] .each --list .length 10 .end)
set_tests_properties(agent_each_list PROPERTIES PASS_REGULAR_EXPRESSION "2 items")

# A body `.exec` is pre-approved once by the parent and runs in each child.
add_test(NAME agent_each_exec
    COMMAND $<TARGET_FILE:prompt> .note [=[| symbol |
| --- |
| a |
| b |]=] .each --yes table:symbol .exec -- echo {symbol} .end)
set_tests_properties(agent_each_exec PROPERTIES PASS_REGULAR_EXPRESSION "## b")
