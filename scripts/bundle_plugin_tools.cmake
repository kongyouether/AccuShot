# Called from application and focused test targets; source exports retain tools/7zip.
function(accushot_bundle_plugin_tools target root)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${root}/tools/7zip" "$<TARGET_FILE_DIR:${target}>/tools/7zip"
        VERBATIM)
endfunction()
