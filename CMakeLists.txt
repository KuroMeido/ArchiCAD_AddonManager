cmake_minimum_required (VERSION 3.20)

# ArchiCAD_AddonManager: standalone Archicad add-on (.apx), independent of the pyArchiCAD solutions.
# Loads any Commands DLL that exports RunCommand (see Shared/CommandsApi.hpp) from a temporary copy, so the
# DLL can be rebuilt and reloaded without restarting Archicad. Manual mode = dialog, Faceless = last command.
#
#   cmake -B build -G "Ninja Multi-Config" -DPython_EXE=<python.exe>
#   cmake --build build --config Release-AC27     ->  out/Release-AC27/ArchiCAD_AddonManager_AC27.apx

set (AC_ADDON_NAME     "ArchiCAD_AddonManager" CACHE STRING   "Add-On name.")
set (AC_ADDON_LANGUAGE "INT"                   CACHE STRING   "Add-On language code.")
set (Python_EXE        "python"                CACHE FILEPATH "Python used by CompileResources.py")

include (${CMAKE_CURRENT_LIST_DIR}/cmake/AcCommon.cmake)
project (ArchiCAD_AddonManager CXX)

file (GLOB_RECURSE SOURCES Src/*.h Src/*.hpp Src/*.cpp)
file (GLOB RESOURCES Resources/RINT/*.grc Resources/RFIX/*.grc Resources/RFIX.win/*.rc2)
source_group ("Resources" FILES ${RESOURCES})

add_library (AddOn SHARED ${SOURCES} ${RESOURCES})
set_source_files_properties (${RESOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_target_properties (AddOn PROPERTIES SUFFIX ".apx")
target_include_directories (AddOn PUBLIC Src Shared)

target_link_options (AddOn PUBLIC
	/export:GetExportedFuncAddrs,@1
	/export:SetImportedFuncAddrs,@2)

ac_apply_versions (AddOn ${AC_ADDON_NAME})

# resources + RES linking, with the DevKit of the active configuration
set (ResourceObjectsDir ${CMAKE_BINARY_DIR}/ResourceObjects)
set (AC_KIT_LIST "")
foreach (ver ${AC_VERSIONS})
	list (APPEND AC_KIT_LIST "${ver}=${AC_DEVKIT_${ver}}")
	set (isAC "$<OR:$<STREQUAL:$<CONFIG>,Debug-AC${ver}>,$<STREQUAL:$<CONFIG>,Release-AC${ver}>>")
	target_link_options (AddOn PUBLIC "$<${isAC}:${ResourceObjectsDir}/AC${ver}/${AC_ADDON_NAME}.res>")
endforeach ()
add_custom_command (TARGET AddOn PRE_BUILD
	COMMAND ${CMAKE_COMMAND} "-DCFG=$<CONFIG>" "-DKITS=${AC_KIT_LIST}" "-DPYTHON=${Python_EXE}"
		"-DLANG=${AC_ADDON_LANGUAGE}" "-DSRC=${CMAKE_CURRENT_LIST_DIR}/Src" "-DRES=${CMAKE_CURRENT_LIST_DIR}/Resources"
		"-DOUT=${ResourceObjectsDir}" "-DNAME=${AC_ADDON_NAME}" -P "${CMAKE_CURRENT_LIST_DIR}/CompileRes.cmake"
	COMMENT "Compiling resources with the DevKit of the active configuration"
	VERBATIM)
