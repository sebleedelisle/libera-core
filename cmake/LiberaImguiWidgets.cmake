get_filename_component(LIBERA_IMGUI_WIDGETS_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(libera_add_imgui_plugin_ui target_name imgui_target)
  add_library(${target_name} STATIC
    "${LIBERA_IMGUI_WIDGETS_ROOT}/src/gui/imgui/PluginManagementPanel.cpp"
  )
  target_include_directories(${target_name}
    PUBLIC
      "${LIBERA_IMGUI_WIDGETS_ROOT}/include"
  )
  target_link_libraries(${target_name}
    PUBLIC
      libera-core
      ${imgui_target}
  )

  if (COMMAND libera_apply_warnings)
    libera_apply_warnings(${target_name})
  endif()
  if (COMMAND libera_apply_sanitizers)
    libera_apply_sanitizers(${target_name})
  endif()
endfunction()
