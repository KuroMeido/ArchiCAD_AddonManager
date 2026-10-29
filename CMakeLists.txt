cmake_minimum_required (VERSION 3.20)

# ArchiCAD_AddonManager solution: standalone Archicad add-on (.apx), independent of the pyArchiCAD solutions.
# The solution file at this folder's root, the project in ArchiCAD_AddonManager/ (same layout as pyKuro).
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

add_subdirectory (ArchiCAD_AddonManager)
ac_write_root_solution (ArchiCAD_AddonManager)
