#ifndef APIIDENTITY_HPP
#define APIIDENTITY_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"

#include <cstddef>

// How an add-on talks to Archicad, read from the disassembly of ACAP_STAT.lib (linked into every add-on):
//   - GetExportedFuncAddrs fills an ExpList: a version, then the wrappers of CheckEnvironment, RegisterInterface,
//     Initialize and FreeData.
//   - SetImportedFuncAddrs copies kImpListSize bytes: the table of Archicad functions.
//   - The Initialize wrapper receives the add-on's client pointer, and every ACAPI_* stub calls one slot of the
//     table with that client pointer as first argument: the client pointer IS the add-on identity.
//   - DllMain opens the module's own resources (ACAPI_GetOwnResModule), independently of Archicad.
// So a module loaded later with LoadLibrary works like a registered add-on once it gets the table and the client
// pointer of this add-on: that is how the manager runs add-ons without restarting Archicad.
#if ACVER != 27
	#error "ACAP_STAT layout measured for the Archicad 27 DevKit: re-measure kImpListSize and the slots for this version"
#endif

namespace AddonManager {
namespace ApiIdentity {

	inline constexpr size_t kImpListSize            = 0x1440;
	inline constexpr size_t kSlotRegisterMenu       = 0x28 / sizeof (void*);	// ACAPI_MenuItem_RegisterMenu
	inline constexpr size_t kSlotInstallMenuHandler = 0xD0 / sizeof (void*);	// ACAPI_MenuItem_InstallMenuHandler

	using InitializeFn = GSErrCode (__ACENV_CALL *) (void* client);

	struct ExpList {
		Int32			version;
		API_AddonType	(__ACENV_CALL *checkEnvironment) (API_EnvirParams* envir);
		GSErrCode		(__ACENV_CALL *registerInterface) (void* addOn);
		InitializeFn	initialize;
		GSErrCode		(__ACENV_CALL *freeData) ();
		unsigned char	rest[0x50];
	};
	static_assert (sizeof (ExpList) == 0x78, "ExpList size of ACAP_STAT");

	bool		Captured ();	// Archicad has given this add-on its function table and its client pointer
	const void*	ImpList ();
	void*		Client ();
	Int32		Version ();		// ExpList version of this add-on: a module built with another DevKit is refused

} // namespace ApiIdentity
} // namespace AddonManager

#endif
