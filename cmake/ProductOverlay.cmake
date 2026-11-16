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

function(jy_app_configure_product_sync target_name)
    set(_product_dir "${PROJECT_ROOT}/products/${JY_APP_PRODUCT}")
    set(_product_lfsd_dir "${PROJECT_ROOT}/products/${JY_APP_PRODUCT}/lfsd")
    set(_product_images_dir "${PROJECT_ROOT}/products/${JY_APP_PRODUCT}/images")
    set(_product_audio_dir "${PROJECT_ROOT}/products/${JY_APP_PRODUCT}/audio")
    set(_sync_stamp "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_product_sync.stamp")

    if(NOT IS_DIRECTORY "${_product_dir}")
        message(FATAL_ERROR "Product directory not found: ${_product_dir}")
    elseif(NOT IS_DIRECTORY "${_product_lfsd_dir}")
        message(FATAL_ERROR "Product LFSD directory not found: ${_product_lfsd_dir}")
    elseif(NOT IS_DIRECTORY "${_product_images_dir}")
        message(FATAL_ERROR "Product images directory not found: ${_product_images_dir}")
    elseif(NOT IS_DIRECTORY "${_product_audio_dir}")
        message(FATAL_ERROR "Product audio directory not found: ${_product_audio_dir}")
    endif()

    file(GLOB_RECURSE _product_lfsd_relative_files
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        RELATIVE "${_product_lfsd_dir}"
        "${_product_lfsd_dir}/*"
    )
    file(GLOB_RECURSE _product_image_relative_files
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        RELATIVE "${_product_images_dir}"
        "${_product_images_dir}/*"
    )
    file(GLOB_RECURSE _product_audio_relative_files
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        RELATIVE "${_product_audio_dir}"
        "${_product_audio_dir}/*"
    )
    set(_product_lfsd_files)
    set(_root_lfsd_files)
    foreach(_relative_file IN LISTS _product_lfsd_relative_files)
        list(APPEND _product_lfsd_files "${_product_lfsd_dir}/${_relative_file}")
        list(APPEND _root_lfsd_files "${PROJECT_ROOT}/lfsd/${_relative_file}")
    endforeach()
    set(_product_image_files)
    set(_root_image_files)
    foreach(_relative_file IN LISTS _product_image_relative_files)
        list(APPEND _product_image_files "${_product_images_dir}/${_relative_file}")
        list(APPEND _root_image_files "${PROJECT_ROOT}/lfsc/system/images/${_relative_file}")
    endforeach()
    set(_product_audio_files)
    set(_root_audio_files)
    foreach(_relative_file IN LISTS _product_audio_relative_files)
        list(APPEND _product_audio_files "${_product_audio_dir}/${_relative_file}")
        list(APPEND _root_audio_files "${PROJECT_ROOT}/lfsc/system/audio/${_relative_file}")
    endforeach()
    add_custom_command(
        OUTPUT "${_sync_stamp}"
        BYPRODUCTS
            "${PROJECT_ROOT}/ui.res.json"
            "${PROJECT_ROOT}/StringPool.csv"
            "${PROJECT_ROOT}/apps/home/home_cfg.c"
            ${_root_lfsd_files}
            ${_root_image_files}
            ${_root_audio_files}
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_ROOT}/scripts/product_sync.py"
                "${JY_APP_PRODUCT}"
        COMMAND ${CMAKE_COMMAND} -E touch "${_sync_stamp}"
        DEPENDS
            "${PROJECT_ROOT}/scripts/product_sync.py"
            "${_product_dir}/StringPool.csv"
            "${_product_dir}/apps/home/home_cfg.c"
            ${_product_lfsd_files}
            ${_product_image_files}
            ${_product_audio_files}
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        COMMENT "Syncing ${JY_APP_PRODUCT} product resources"
        VERBATIM
    )
    add_custom_target(${target_name}_product_sync DEPENDS "${_sync_stamp}")
    add_dependencies(${target_name} ${target_name}_product_sync)
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS "${_sync_stamp}")
endfunction()

function(jy_app_resolve_left_romfs output_var)
    set(_product_left_romfs "${PROJECT_ROOT}/products/${JY_APP_PRODUCT}/left_romfs")
    if(IS_DIRECTORY "${_product_left_romfs}")
        set(_selected_left_romfs "${_product_left_romfs}")
        set(_left_romfs_source "product override")
    else()
        set(_selected_left_romfs "${PROJECT_ROOT}/left_romfs")
        set(_left_romfs_source "repository default")
    endif()

    message(STATUS
        "Selected left ROMFS (${_left_romfs_source}): ${_selected_left_romfs}")
    set(${output_var} "${_selected_left_romfs}" PARENT_SCOPE)
endfunction()
