# Incbin is resolved by the assembler from its working directory.
target_compile_options(${PROJECT_NAME} PRIVATE
    "-Wa,-I${SDK_ROOT}/src/${PROJ}" -fno-fast-math)
if(PROJ STREQUAL "greeter_count")
    target_compile_definitions(${PROJECT_NAME} PRIVATE GREETER_ENABLE_COUNTING=1)
endif()
