#ifndef ADDINMANAGERDIALOG_HPP
#define ADDINMANAGERDIALOG_HPP

#include <cstddef>	// wchar_t is a typedef here (/Zc:wchar_t- required by the Archicad DevKit)
#include <string>

namespace AddonManager {
namespace AddonManagerDialog {

	struct Selection {
		std::wstring	dllPath;	// in: last DLL; out: the DLL to run
		std::string		toolName;	// in: last command; out: the command to run
	};

	// Modal window (Win32, no resources): pick a Commands DLL, see its commands, choose one to run.
	// Returns true when the user pressed Run (or double-clicked a command).
	bool Show (Selection& selection);

} // namespace AddonManagerDialog
} // namespace AddonManager

#endif
