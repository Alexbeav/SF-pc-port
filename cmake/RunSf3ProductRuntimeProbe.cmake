if(NOT DEFINED SF_PROBE OR NOT EXISTS "${SF_PROBE}")
    message(FATAL_ERROR "SF_PROBE must name the built sf_tool executable")
endif()
if(NOT DEFINED SF_ROM_CUE OR NOT EXISTS "${SF_ROM_CUE}")
    message(FATAL_ERROR "SF_ROM_CUE must name the owned SCUS-94640 CUE")
endif()
if(NOT DEFINED SF_FRAMES)
    set(SF_FRAMES 300)
endif()

function(sf_run_product_probe label output_var)
    execute_process(
        COMMAND "${SF_PROBE}" probe-sf3-product-runtime
            "${SF_ROM_CUE}" "${SF_FRAMES}" neutral
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "SF3 product-runtime ${label} failed (${result}).\n${error}${output}")
    endif()
    if(NOT output MATCHES "SF3 product runtime: mode=neutral requested=${SF_FRAMES}")
        message(FATAL_ERROR
            "SF3 product-runtime ${label} omitted its completion record.\n${output}")
    endif()
    set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

sf_run_product_probe("run 1" first_output)
sf_run_product_probe("run 2" second_output)

string(SHA256 first_sha256 "${first_output}")
string(SHA256 second_sha256 "${second_output}")
if(NOT first_output STREQUAL second_output OR
   NOT first_sha256 STREQUAL second_sha256)
    message(FATAL_ERROR
        "SF3 product-runtime stdout diverged: ${first_sha256} != ${second_sha256}")
endif()

string(LENGTH "${first_output}" output_bytes)
message(STATUS
    "SF3 product-runtime deterministic: frames=${SF_FRAMES} bytes=${output_bytes} sha256=${first_sha256}")
