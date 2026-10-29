# Compiles the .grc resources with the DevKit that belongs to the active configuration.
# Called from CMakeLists.txt:  cmake -DCFG=<config> -DKITS="27=dir;28=dir" -DPYTHON=.. -DLANG=INT
#                                    -DSRC=.. -DRES=.. -DOUT=.. -DNAME=.. -P CompileRes.cmake
string (REGEX MATCH "AC([0-9][0-9])$" _m "${CFG}")
if (NOT _m)
	message (FATAL_ERROR "Configuration '${CFG}' must end with AC<NN>, e.g. Release-AC27")
endif ()
set (ver ${CMAKE_MATCH_1})

set (kit "")
foreach (entry ${KITS})
	if (entry MATCHES "^${ver}=(.+)$")
		set (kit "${CMAKE_MATCH_1}")
	endif ()
endforeach ()
if (kit STREQUAL "")
	message (FATAL_ERROR "No DevKit registered for AC${ver}")
endif ()

file (MAKE_DIRECTORY "${OUT}/AC${ver}")
execute_process (
	COMMAND "${PYTHON}" "${kit}/Support/Tools/CompileResources.py" "${LANG}" "${kit}" "${SRC}" "${RES}" "${OUT}/AC${ver}" "${OUT}/AC${ver}/${NAME}.res"
	RESULT_VARIABLE rc)
if (NOT rc EQUAL 0)
	message (FATAL_ERROR "Resource compilation failed for AC${ver} (exit ${rc})")
endif ()
