#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Lib/System/ConstantsAndMessages.hpp"
#include "Panels/Panel_Manager.hpp"

#include <cstdio>

using namespace AddonManager;

namespace {

	// Trace of the add-on entry points: %TEMP%\ArchiCAD_AddonManager.log (tells whether Archicad loaded the add-on).
	void Trace (const char* text, int value = 0)
	{
		char path[MAX_PATH] = {};
		if (GetTempPathA (MAX_PATH, path) == 0)
			return;
		strcat_s (path, "ArchiCAD_AddonManager.log");
		FILE* f = nullptr;
		if (fopen_s (&f, path, "a") == 0 && f != nullptr) {
			fprintf (f, "%s %d\n", text, value);
			fclose (f);
		}
	}

	GSErrCode __ACENV_CALL MenuCommandHandler (const API_MenuParams* menuParams)
	{
		Trace ("menu command", menuParams->menuItemRef.itemIndex);
		if (menuParams->menuItemRef.menuResID == ConstantsAndMessages::MENU_STRINGS_MANUAL)
			Panels::Panel_Manager_HandleMenuItem (ConstantsAndMessages::MENU_MANAGER_MANUAL);
		else if (menuParams->menuItemRef.menuResID == ConstantsAndMessages::MENU_STRINGS_FACELESS)
			Panels::Panel_Manager_HandleMenuItem (ConstantsAndMessages::MENU_MANAGER_FACELESS);
		return NoError;
	}

} // namespace

API_AddonType __ACENV_CALL CheckEnvironment (API_EnvirParams* envir)
{
	Trace ("CheckEnvironment");
	RSGetIndString (&envir->addOnInfo.name, ConstantsAndMessages::ADDON_NAME_STRINGS, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, ConstantsAndMessages::ADDON_NAME_STRINGS, 2, ACAPI_GetOwnResModule ());
	return APIAddon_Preload;	// Normal add-ons are only fully loaded once one of their commands is placed in the Work Environment
}

GSErrCode __ACENV_CALL RegisterInterface (void)
{
	// Own main menu ("tab") in the menu bar, like Tapir
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (ConstantsAndMessages::MENU_STRINGS_MANUAL, 0, MenuCode_UserDef, MenuFlag_Default);
	if (err == NoError)
		err = ACAPI_MenuItem_RegisterMenu (ConstantsAndMessages::MENU_STRINGS_FACELESS, 0, MenuCode_UserDef, MenuFlag_Default);
	Trace ("RegisterInterface err", static_cast<int> (err));
	return err;
}

GSErrCode __ACENV_CALL Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ConstantsAndMessages::MENU_STRINGS_MANUAL, MenuCommandHandler);
	if (err == NoError)
		err = ACAPI_MenuItem_InstallMenuHandler (ConstantsAndMessages::MENU_STRINGS_FACELESS, MenuCommandHandler);
	Trace ("Initialize err", static_cast<int> (err));
	return err;
}

GSErrCode __ACENV_CALL FreeData (void)
{
	Trace ("FreeData");
	return NoError;
}
