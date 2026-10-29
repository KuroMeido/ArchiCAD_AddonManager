#ifndef ADDONMANAGERDIALOG_HPP
#define ADDONMANAGERDIALOG_HPP

#include <cstddef>	// wchar_t is a typedef here (/Zc:wchar_t- required by the Archicad DevKit)
#include <string>

namespace AddonManager {
namespace AddonManagerDialog {

	struct Selection {
		std::wstring	path;		// in: last add-on / DLL; out: the one to run
		std::wstring	command;	// in: last command key; out: the command to run (see CommandLoader::Command)
	};

	// Modal window (Win32, no resources): pick an add-on (.apx) or a Commands DLL, see its commands, choose one.
	// Returns true when the user pressed Run (or double-clicked a command).
	bool Show (Selection& selection);

} // namespace AddonManagerDialog
} // namespace AddonManager

#endif
