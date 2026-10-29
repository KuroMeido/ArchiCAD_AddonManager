cmake_minimum_required (VERSION 3.20)

# ArchiCAD_AddonManager: standalone Archicad add-on (.apx), independent of the pyArchiCAD solutions.
# Runs the menu commands of an add-on (.apx), or the RunCommand tools of a Commands DLL (see Shared/CommandsApi.hpp),
# from a temporary copy, so they can be rebuilt and reloaded without restarting Archicad.
# Manual mode = dialog, Faceless = last command.
#
# Visual Studio solution (v142): cmake --preset ac27-vs  ->  ArchiCAD_AddonManager.slnx at this folder's root.
# Ninja (portable MSVC): cmake --preset ac27 ; cmake --build --preset release-ac27
# Output: out/Release-AC27/ArchiCAD_AddonManager_AC27.apx

set (AC_ADDON_NAME     "ArchiCAD_AddonManager" CACHE STRING   "Add-On name.")
set (AC_ADDON_LANGUAGE "INT"                   CACHE STRING   "Add-On language code.")
set (Python_EXE        "python"                CACHE FILEPATH "Python used by CompileResources.py")

include (${CMAKE_CURRENT_LIST_DIR}/cmake/AcCommon.cmake)
project (ArchiCAD_AddonManager CXX)

file (GLOB_RECURSE SOURCES Src/*.h Src/*.hpp Src/*.cpp)
file (GLOB RESOURCES Resources/RINT/*.grc Resources/RFIX/*.grc Resources/RFIX.win/*.rc2)
source_group ("Resources" FILES ${RESOURCES})

add_library (ArchiCAD_AddonManager SHARED ${SOURCES} ${RESOURCES})
set_source_files_properties (${RESOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_target_properties (ArchiCAD_AddonManager PROPERTIES SUFFIX ".apx")
target_include_directories (ArchiCAD_AddonManager PUBLIC Src Shared)

# The entry points are exported through wrappers (Src/Lib/Execute/ApiIdentity.cpp) that keep the Archicad
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
		"-DLANG=${AC_ADDON_LANGUAGE}" "-DSRC=${CMAKE_CURRENT_LIST_DIR}/Src" "-DRES=${CMAKE_CURRENT_LIST_DIR}/Resources"
		"-DOUT=${ResourceObjectsDir}" "-DNAME=${AC_ADDON_NAME}" -P "${CMAKE_CURRENT_LIST_DIR}/CompileRes.cmake"
	COMMENT "Compiling resources with the DevKit of the active configuration"
	VERBATIM)

# Visual Studio generator: the solution also at the root of this folder (ArchiCAD_AddonManager.slnx);
# its projects stay in the build folder (build-vs), rewritten by every configure.
if (CMAKE_GENERATOR MATCHES "^Visual Studio")
	file (RELATIVE_PATH buildRel "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
	set (slnConfigs "")
	foreach (cfg ${CMAKE_CONFIGURATION_TYPES})
		string (APPEND slnConfigs "\t\t<BuildType Name=\"${cfg}\"/>\n")
	endforeach ()
	set (vcxType "8bc9ceb8-8b4a-11d0-8d11-00a0c91bc942")	# C++ project: the IDE needs it, MSBuild guesses it
	file (CONFIGURE OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/ArchiCAD_AddonManager.slnx" @ONLY CONTENT
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!-- Written by CMakeLists.txt (configure preset ac27-vs): edit CMake, not this file. -->
<Solution>
	<Configurations>
@slnConfigs@		<Platform Name=\"x64\"/>
	</Configurations>
	<Project Path=\"@buildRel@/ArchiCAD_AddonManager.vcxproj\" Type=\"@vcxType@\" DefaultStartup=\"true\"/>
</Solution>
")
endif ()
