idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES esp_container wasm-micro-runtime heap)
if(NOT WAMR_BUILD_INSTRUCTION_METERING)
    message(FATAL_ERROR "C3 probe requires WAMR Classic instruction metering")
endif()
# WAMR keeps its compile definition private; this sample checks the same
# pre-project build setting in its own source profile assertion.
target_compile_definitions(${COMPONENT_LIB} PRIVATE WASM_ENABLE_INSTRUCTION_METERING=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
