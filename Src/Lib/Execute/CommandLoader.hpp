#ifndef COMMANDLOADER_HPP
#define COMMANDLOADER_HPP

#include <cstddef>	// wchar_t is a typedef here (/Zc:wchar_t- required by the Archicad DevKit)
#include <string>
#include <vector>

namespace AddonManager {
namespace CommandLoader {

	// Something the manager can run. key: "menu:<STR# id>:<item index>" (a menu item of an add-on) or
	// "run:<tool name>" (RunCommand of a Commands DLL); a key without prefix is a tool name.
	struct Command {
		std::wstring	key;
		std::wstring	label;
	};

	struct ModuleInfo {
		std::wstring			name;			// from the add-on's CheckEnvironment
		std::wstring			description;
		std::wstring			kind;
		std::vector<Command>	commands;
	};

	// Loads a temporary copy of an add-on (.apx) or of a Commands DLL, with the content of its folder (DLLs, data
	// files, subfolders; not other add-ons nor build leftovers, at most 300 MB), so the original
	// stays rebuildable while Archicad runs. An add-on runs under the manager's identity: its CheckEnvironment,
	// RegisterInterface and Initialize run, its menu handlers are kept instead of being installed in Archicad.
	// The module stays loaded until the next load (windows or callbacks it left behind stay valid).
	bool	Inspect (const std::wstring& path, ModuleInfo& info, std::wstring& error);

	// Fresh copy of `path`, then runs `key`. Returns a GSErrCode (0 = NoError); errors are reported in Archicad.
	int		Run (const std::wstring& path, const std::wstring& key);

	// Unloads the module of the last Inspect / Run (call from FreeData).
	void	UnloadAll ();

} // namespace CommandLoader
} // namespace AddonManager

#endif
