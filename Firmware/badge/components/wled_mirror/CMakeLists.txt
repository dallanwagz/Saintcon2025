# wled_mirror: badge glue between the wled_fx engine, the LED layer manager, badge_config and HA Menu.
set(presets_c ${CMAKE_CURRENT_BINARY_DIR}/wm_presets.c)

idf_component_register(
    SRCS wled_mirror.c ${presets_c}
    INCLUDE_DIRS include
    REQUIRES badge wled_fx
    PRIV_REQUIRES led esp_timer freertos)

# The preset table is generated from the local, git-ignored JSON (CONFIG_WLED_PRESETS_FILE). The generator runs
# on every build (cheap) and only rewrites wm_presets.c when it changes, so creating, editing or deleting the
# JSON is picked up without a reconfigure; a missing file gives an empty table.
# ponytail: an always-run target instead of add_custom_command(DEPENDS json), which needs a reconfigure when the file appears.
idf_build_get_property(python PYTHON)
idf_build_get_property(project_dir PROJECT_DIR)
set(presets_json "${CONFIG_WLED_PRESETS_FILE}")
if(presets_json AND NOT IS_ABSOLUTE "${presets_json}")
    set(presets_json "${project_dir}/${presets_json}")
endif()
set(gen ${project_dir}/tools/wled_port/gen_presets_c.py)
add_custom_target(wled_mirror_presets
    COMMAND ${python} ${gen} "${presets_json}" ${presets_c}
    BYPRODUCTS ${presets_c}
    COMMENT "wled_mirror: preset table from ${CONFIG_WLED_PRESETS_FILE}"
    VERBATIM)
set_source_files_properties(${presets_c} PROPERTIES GENERATED TRUE)
add_dependencies(${COMPONENT_LIB} wled_mirror_presets)
