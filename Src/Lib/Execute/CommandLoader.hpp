#ifndef COMMANDLOADER_HPP
#define COMMANDLOADER_HPP

#include <cstddef>	// wchar_t is a typedef here (/Zc:wchar_t- required by the Archicad DevKit)
#include <string>
#include <vector>

namespace AddonManager {
namespace CommandLoader {

	// Loads a temporary copy of a Commands DLL (the original stays rebuildable while Archicad runs), hands it
	// the Archicad function table, runs the command registered under `toolName`, unloads it.
	// Returns a GSErrCode (0 = NoError).
	int RunFrom (const std::wstring& dllPath, const char* toolName);

	// Tool names of a DLL through its optional ListCommands export. Returns false (and a reason in `error`)
	// when the DLL cannot be loaded; true with an empty list when it has no ListCommands.
	bool ListCommands (const std::wstring& dllPath, std::vector<std::string>& names, std::wstring& error);

} // namespace CommandLoader
} // namespace AddonManager

#endif
