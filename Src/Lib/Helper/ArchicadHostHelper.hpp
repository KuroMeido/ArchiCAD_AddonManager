#ifndef ARCHICADHOSTHELPER_HPP
#define ARCHICADHOSTHELPER_HPP

#include "CommandsApi.hpp"

namespace AddonManager {
namespace ArchicadHostHelper {

	// Implements SheetXL_Host with ACAPI_* calls. Lives in the UI add-on because only the registered
	// add-on module carries the add-on identity Archicad requires for API calls.
	struct Context;		// snapshot of the Layout Book (masters, subsets, layout IDs), see the .cpp

	Context*	CreateContext ();					// takes the snapshot (call inside the menu handler)
	void		DestroyContext (Context* ctx);
	SheetXL_Host MakeHost (Context* ctx);

} // namespace ArchicadHostHelper
} // namespace AddonManager

#endif
