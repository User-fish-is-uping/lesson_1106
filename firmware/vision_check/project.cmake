# Incbin is resolved by the assembler from its working directory.
target_compile_options(${PROJECT_NAME} PRIVATE
    "-Wa,-I${SDK_ROOT}/src/${PROJ}" -fno-fast-math)
