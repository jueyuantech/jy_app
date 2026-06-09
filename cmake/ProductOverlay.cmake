include_guard(GLOBAL)

set(JY_APP_PRODUCT "jytek" CACHE STRING "Product overlay name to apply before collecting sources")

function(jy_app_run_product_sync command)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_ROOT}/scripts/product_sync.py" "${command}"
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        RESULT_VARIABLE _product_sync_result
    )
    if(NOT _product_sync_result EQUAL 0)
        message(FATAL_ERROR "product_sync.py ${command} failed with exit code ${_product_sync_result}")
    endif()
endfunction()

function(jy_app_apply_product_overlay)
    if(JY_APP_PRODUCT STREQUAL "clean")
        message(FATAL_ERROR
            "JY_APP_PRODUCT=clean only removes product overlay files and cannot produce a build. "
            "Use a product name, for example -DJY_APP_PRODUCT=jytek.")
    elseif(NOT JY_APP_PRODUCT STREQUAL "")
        message(STATUS "Applying product overlay: ${JY_APP_PRODUCT}")
        jy_app_run_product_sync(clean)
        jy_app_run_product_sync("${JY_APP_PRODUCT}")
    endif()
endfunction()
