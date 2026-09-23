idf_component_register(SRCS "src/wasm_scan.c" "src/runtime.c"
    "src/package_stream.c" "src/package_crypto.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES wasm-micro-runtime esp_timer mbedtls)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT CONFIG_WAMR_ENABLE_INTERP OR NOT CONFIG_WAMR_INTERP_CLASSIC OR
       NOT CONFIG_WAMR_INTERP_LOADER_NORMAL OR CONFIG_WAMR_INTERP_FAST OR
       CONFIG_WAMR_INTERP_LOADER_MINI OR CONFIG_WAMR_ENABLE_AOT OR
       CONFIG_WAMR_ENABLE_LIBC_BUILTIN OR CONFIG_WAMR_ENABLE_LIBC_WASI OR
       CONFIG_WAMR_ENABLE_LIB_PTHREAD OR CONFIG_WAMR_ENABLE_MULTI_MODULE OR
       CONFIG_WAMR_ENABLE_SHARED_MEMORY OR CONFIG_WAMR_ENABLE_REF_TYPES OR
       NOT WAMR_BUILD_INSTRUCTION_METERING OR WAMR_BUILD_AOT OR
       WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_MINI_LOADER OR
       WAMR_BUILD_LIBC_BUILTIN OR WAMR_BUILD_LIBC_WASI OR
       WAMR_BUILD_LIB_PTHREAD OR WAMR_BUILD_MULTI_MODULE OR
       WAMR_BUILD_REF_TYPES OR WAMR_BUILD_SIMD OR
       WAMR_BUILD_BULK_MEMORY OR WAMR_BUILD_SHARED_MEMORY OR
       WAMR_BUILD_JIT OR WAMR_BUILD_FAST_JIT)
        message(FATAL_ERROR "ESP Container requires WAMR Classic/normal loader with instruction metering; AOT/Fast/WASI/pthread/multi-module/ref-types/SIMD/bulk/shared memory must be disabled. Use examples/c3-runtime/sdkconfig.defaults and its CMake profile before project()")
    endif()
    idf_build_get_property(sdk_python PYTHON)
    idf_build_get_property(sdk_path IDF_PATH)
    execute_process(COMMAND "${sdk_python}" "${CMAKE_CURRENT_LIST_DIR}/tools/check_sdk.py"
        --path "${sdk_path}"
        RESULT_VARIABLE sdk_result ERROR_VARIABLE sdk_error OUTPUT_QUIET)
    if(NOT sdk_result STREQUAL "0")
        message(FATAL_ERROR "${sdk_error}")
    endif()
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
