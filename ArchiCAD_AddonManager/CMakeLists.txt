# ArchiCAD_AddonManager project: App (entry points), Panels (menu), Lib/Execute (hot-loading), PresentationWPF (dialog).

file (GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.h *.hpp *.cpp)
file (GLOB RESOURCES Resources/RINT/*.grc Resources/RFIX/*.grc Resources/RFIX.win/*.rc2)
source_group (TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${RESOURCES})

add_library (ArchiCAD_AddonManager SHARED ${SOURCES} ${RESOURCES})
set_source_files_properties (${RESOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_target_properties (ArchiCAD_AddonManager PROPERTIES SUFFIX ".apx")
target_include_directories (ArchiCAD_AddonManager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} Shared)

# The entry points are exported through wrappers (Lib/Execute/ApiIdentity.cpp) that keep the Archicad
# function table and client pointer, so add-ons loaded later can run under this add-on's identity.
target_link_options (ArchiCAD_AddonManager PUBLIC
	/export:GetExportedFuncAddrs=AM_GetExportedFuncAddrs,@1
	/export:SetImportedFuncAddrs=AM_SetImportedFuncAddrs,@2)

ac_apply_versions (ArchiCAD_AddonManager ${AC_ADDON_NAME})

# resources + RES linking, with the DevKit of the active configuration
set (ResourceObjectsDir ${CMAKE_CURRENT_BINARY_DIR}/ResourceObjects)
set (AC_KIT_LIST "")
foreach (ver ${AC_VERSIONS})
	list (APPEND AC_KIT_LIST "${ver}=${AC_DEVKIT_${ver}}")
	set (isAC "$<OR:$<STREQUAL:$<CONFIG>,Debug-AC${ver}>,$<STREQUAL:$<CONFIG>,Release-AC${ver}>>")
	target_link_options (ArchiCAD_AddonManager PUBLIC "$<${isAC}:${ResourceObjectsDir}/AC${ver}/${AC_ADDON_NAME}.res>")
endforeach ()
add_custom_command (TARGET ArchiCAD_AddonManager PRE_BUILD
	COMMAND ${CMAKE_COMMAND} "-DCFG=$<CONFIG>" "-DKITS=${AC_KIT_LIST}" "-DPYTHON=${Python_EXE}"
		"-DLANG=${AC_ADDON_LANGUAGE}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}" "-DRES=${CMAKE_CURRENT_SOURCE_DIR}/Resources"
		"-DOUT=${ResourceObjectsDir}" "-DNAME=${AC_ADDON_NAME}" -P "${CMAKE_SOURCE_DIR}/cmake/CompileRes.cmake"
	COMMENT "Compiling resources with the DevKit of the active configuration"
	VERBATIM)
