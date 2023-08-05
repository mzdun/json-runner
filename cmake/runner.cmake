set(VS_MODS)

function(fix_vs_modules TARGET)
  set(__TOOLS ${VS_MODS} ${TARGET})
  set(VS_MODS ${__TOOLS} PARENT_SCOPE)

  if (MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE)
    set_target_properties(${TARGET} PROPERTIES VS_USER_PROPS "${PROJECT_SOURCE_DIR}/cmake/VS17.NoModules.props")
  endif()
endfunction()

macro(set_parent_scope)
  set(VS_MODS ${VS_MODS} PARENT_SCOPE)
endmacro()
