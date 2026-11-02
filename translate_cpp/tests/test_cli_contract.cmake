file(WRITE "contract-input.txt" "First original paragraph.\n\nSecond original paragraph.\n")
file(WRITE "contract-model.gguf" "stand-in model")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env TRANSLATE_TEST_INCOMPLETE=1
    "${TOOL}" contract-input.txt -o contract-output.txt --model-file contract-model.gguf -s en -t de
    RESULT_VARIABLE status OUTPUT_VARIABLE log ERROR_VARIABLE errors)
if(NOT status EQUAL 3)
    message(FATAL_ERROR "Expected partial-success exit 3, got ${status}: ${log} ${errors}")
endif()
file(READ "contract-input.txt" original)
file(READ "contract-output.txt" output)
if(NOT output STREQUAL original)
    message(FATAL_ERROR "Failed translations did not retain the complete original input")
endif()
file(REMOVE contract-input.txt contract-model.gguf contract-output.txt)
