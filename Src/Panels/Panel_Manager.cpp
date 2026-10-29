#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Panels/Panel_Manager.hpp"

#include "Lib/Execute/CommandLoader.hpp"
#include "Lib/Helper/AddonManagerDialog.hpp"
#include "Lib/System/ConstantsAndMessages.hpp"

#include <shlobj.h>
#include <string>

namespace AddonManager {
namespace Panels {

namespace {

	// The last add-on / DLL + command, kept in %APPDATA%\AddonManager\AddonManager.ini so Faceless mode survives a restart.
	std::wstring IniPath ()
	{
		wchar_t dir[MAX_PATH] = {};
		if (FAILED (SHGetFolderPathW (nullptr, CSIDL_APPDATA, nullptr, 0, dir)))
			return L"";
		const std::wstring folder = std::wstring (dir) + L"\\AddonManager";
		CreateDirectoryW (folder.c_str (), nullptr);
		return folder + L"\\AddonManager.ini";
	}

	std::wstring ReadSetting (const wchar_t* key)
	{
		wchar_t buf[MAX_PATH] = {};
		GetPrivateProfileStringW (L"Last", key, L"", buf, MAX_PATH, IniPath ().c_str ());
		return buf;
	}

	void WriteSetting (const wchar_t* key, const std::wstring& value)
	{
		WritePrivateProfileStringW (L"Last", key, value.c_str (), IniPath ().c_str ());
	}

	void Remember (const AddonManagerDialog::Selection& sel)
	{
		WriteSetting (L"Path", sel.path);
		WriteSetting (L"Command", sel.command);
	}

	AddonManagerDialog::Selection LoadLast ()
	{
		AddonManagerDialog::Selection sel;
		sel.path = ReadSetting (L"Path");
		if (sel.path.empty ())
			sel.path = ReadSetting (L"Dll");	// written by the first versions
		sel.command = ReadSetting (L"Command");
		return sel;
	}

	// Manual mode: dialog, then run what was chosen.
	void RunManual ()
	{
		AddonManagerDialog::Selection sel = LoadLast ();
		if (!AddonManagerDialog::Show (sel))
			return;
		Remember (sel);
		CommandLoader::Run (sel.path, sel.command);
	}

	// Faceless mode: the last command again (fresh copy of the add-on / DLL), no manager window.
	void RunFaceless ()
	{
		const AddonManagerDialog::Selection sel = LoadLast ();
		if (sel.path.empty () || sel.command.empty ()) {
			ACAPI_WriteReport ("Add-in Manager: nothing to repeat yet. Run a command in Manual Mode first.", true);
			return;
		}
		CommandLoader::Run (sel.path, sel.command);
	}

} // namespace

	bool Panel_Manager_HandleMenuItem (short itemIndex)
	{
		switch (itemIndex) {
			case ConstantsAndMessages::MENU_MANAGER_MANUAL:
				RunManual ();
				return true;
			case ConstantsAndMessages::MENU_MANAGER_FACELESS:
				RunFaceless ();
				return true;
		}
		return false;
	}

} // namespace Panels
} // namespace AddonManager
