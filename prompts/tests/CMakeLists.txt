add_executable(test_chaining test_chaining.cpp) target_link_libraries(test_chaining PRIVATE prompt_lib)
    target_include_directories(test_chaining PRIVATE ${CMAKE_SOURCE_DIR} / src)

        add_test(NAME chaining COMMAND test_chaining)

            add_executable(test_errors test_errors.cpp) target_link_libraries(test_errors PRIVATE prompt_lib)
                target_include_directories(test_errors PRIVATE ${CMAKE_SOURCE_DIR} / src)

                    add_test(NAME errors COMMAND test_errors)

                        add_executable(test_fs test_fs.cpp) target_link_libraries(test_fs PRIVATE prompt_lib)
                            target_include_directories(test_fs PRIVATE ${CMAKE_SOURCE_DIR} / src)

                                add_test(NAME fs COMMAND test_fs)

                                    add_executable(test_sanitize test_sanitize.cpp)
                                        target_link_libraries(test_sanitize PRIVATE prompt_lib)
                                            target_include_directories(test_sanitize PRIVATE ${CMAKE_SOURCE_DIR} / src)

                                                add_test(NAME sanitize COMMAND test_sanitize)
