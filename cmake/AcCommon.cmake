# Shared by pyArchiCAD_UI, pyArchiCAD_Cmd and ArchiCAD_AddonManager (one copy each): discovers Archicad DevKits and creates one configuration per version
# (Debug-AC<NN> / Release-AC<NN>). Include BEFORE project(), then call ac_apply_versions(<target> <base name>).
#
# Options (cache):
#   AC_DEVKITS   "27=C:/dir;28=C:/dir"   empty = auto-discover "C:/Program Files/GRAPHISOFT/API Development Kit <ver>.*"
#   AC_MIN_VERSION                        oldest supported version (layout API below 26 is not implemented)

set (AC_MIN_VERSION 26 CACHE STRING "Oldest supported Archicad.")
set (AC_DEVKITS     "" CACHE STRING "List of <ver>=<DevKit dir>; empty = auto-discover.")
set (AC_OUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../out" CACHE PATH "Both modules are written to <AC_OUT_DIR>/<Config>.")

set (AC_VERSIONS "")
function (_register_devkit ver dir)
	if (NOT EXISTS "${dir}/Support/Inc/ACAPinc.h")
		message (WARNING "Not a DevKit, ignored: ${dir}")
		return ()
	endif ()
	if (ver LESS AC_MIN_VERSION)
		return ()
	endif ()
	list (FIND AC_VERSIONS ${ver} idx)
	if (idx EQUAL -1)
		set (AC_VERSIONS ${AC_VERSIONS} ${ver} PARENT_SCOPE)
		set (AC_DEVKIT_${ver} "${dir}" PARENT_SCOPE)
	endif ()
endfunction ()

foreach (entry ${AC_DEVKITS})
	string (REGEX MATCH "^([0-9][0-9])=(.+)$" _m "${entry}")
	if (_m)
		_register_devkit (${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
	endif ()
endforeach ()
if (NOT AC_VERSIONS)
	file (GLOB _kits "C:/Program Files/GRAPHISOFT/API Development Kit *")
	foreach (kit ${_kits})
		string (REGEX MATCH "API Development Kit ([0-9][0-9])" _m "${kit}")
		if (_m)
			_register_devkit (${CMAKE_MATCH_1} "${kit}")
		endif ()
	endforeach ()
endif ()
if (NOT AC_VERSIONS)
	message (FATAL_ERROR "No Archicad API DevKit (>= ${AC_MIN_VERSION}) found. Use -DAC_DEVKITS=\"27=<dir>\"")
endif ()
list (SORT AC_VERSIONS)

set (CMAKE_CONFIGURATION_TYPES "")
foreach (ver ${AC_VERSIONS})
	list (APPEND CMAKE_CONFIGURATION_TYPES Debug-AC${ver} Release-AC${ver})
endforeach ()
set (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set (CMAKE_SUPPRESS_REGENERATION 1)

# Applies compiler flags, per-version DevKit includes/libs, ACVER, output name and output folder.
# NO_ACAPI: plain DLL that makes no ACAPI_* call (Commands): do not link ACAP_STAT.lib.
function (ac_apply_versions target base_name)
	cmake_parse_arguments (ARG "NO_ACAPI" "" "" ${ARGN})
	set_target_properties (${target} PROPERTIES VS_PLATFORM_TOOLSET v142)
	target_compile_features (${target} PUBLIC cxx_std_17)
	target_compile_options (${target} PUBLIC /W3 /WX /wd4996 /Zc:wchar_t-)
	target_compile_definitions (${target} PUBLIC UNICODE _UNICODE _ITERATOR_DEBUG_LEVEL=0 ACExtension)

	foreach (ver ${AC_VERSIONS})
		set (kit "${AC_DEVKIT_${ver}}")
		set (isAC "$<OR:$<STREQUAL:$<CONFIG>,Debug-AC${ver}>,$<STREQUAL:$<CONFIG>,Release-AC${ver}>>")
		message (STATUS "${target} AC${ver}: ${kit}")

		foreach (cfg Debug-AC${ver} Release-AC${ver})
			string (TOUPPER "${cfg}" CFG_UPPER)
			set_target_properties (${target} PROPERTIES
				OUTPUT_NAME_${CFG_UPPER} "${base_name}_AC${ver}"
				RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} "${AC_OUT_DIR}/${cfg}")
		endforeach ()

		target_compile_definitions (${target} PUBLIC
			"$<${isAC}:ACVER=${ver}>"
			"$<$<STREQUAL:$<CONFIG>,Debug-AC${ver}>:DEBUG>")
		target_include_directories (${target} PUBLIC "$<${isAC}:${kit}/Support/Inc>")
		if (NOT ARG_NO_ACAPI)
			target_link_options (${target} PUBLIC "$<${isAC}:${kit}/Support/Lib/ACAP_STAT.lib>")
		endif ()

		file (GLOB moduleDirs "${kit}/Support/Modules/*")
		foreach (dir ${moduleDirs})
			target_include_directories (${target} PUBLIC "$<${isAC}:${dir}>")
			file (GLOB libs "${dir}/Win/*.lib")
			foreach (lib ${libs})
				target_link_options (${target} PUBLIC "$<${isAC}:${lib}>")
			endforeach ()
		endforeach ()
	endforeach ()
endfunction ()

# Visual Studio generator: also writes <solution folder>/<solution>.slnx, like a .sln next to its project folder;
# the project itself stays in the build folder (build-vs/<target>), rewritten by every configure.
function (ac_write_root_solution target)
	if (NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
		return ()
	endif ()
	get_target_property (targetBinDir ${target} BINARY_DIR)
	file (RELATIVE_PATH buildRel "${CMAKE_SOURCE_DIR}" "${targetBinDir}")
	set (slnConfigs "")
	foreach (cfg ${CMAKE_CONFIGURATION_TYPES})
		string (APPEND slnConfigs "\t\t<BuildType Name=\"${cfg}\"/>\n")
	endforeach ()
	set (vcxType "8bc9ceb8-8b4a-11d0-8d11-00a0c91bc942")	# C++ project: the IDE needs it, MSBuild guesses it
	file (CONFIGURE OUTPUT "${CMAKE_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.slnx" @ONLY CONTENT
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!-- Written by CMakeLists.txt (configure preset ac27-vs): edit CMake, not this file. -->
<Solution>
	<Configurations>
@slnConfigs@		<Platform Name=\"x64\"/>
	</Configurations>
	<Project Path=\"@buildRel@/@target@.vcxproj\" Type=\"@vcxType@\" DefaultStartup=\"true\"/>
</Solution>
")
endfunction ()


