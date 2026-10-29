#include "Lib/Execute/ApiIdentity.hpp"

#include <cstring>

// The ACAP_STAT.lib implementations. This add-on exports the AM_ wrappers below under these names
// (see the /export options in CMakeLists.txt), so it sees what Archicad hands over.
extern "C" Int32 __ACENV_CALL GetExportedFuncAddrs (void* expList);
extern "C" Int32 __ACENV_CALL SetImportedFuncAddrs (const void* impList);

namespace {

	unsigned char	gImpList[AddonManager::ApiIdentity::kImpListSize];
	bool			gImpListSet = false;
	void*			gClient = nullptr;
	Int32			gVersion = 0;
	AddonManager::ApiIdentity::InitializeFn	gAcapInitialize = nullptr;

	GSErrCode __ACENV_CALL AM_Initialize (void* client)
	{
		gClient = client;
		return gAcapInitialize (client);
	}

} // namespace

extern "C" Int32 __ACENV_CALL AM_GetExportedFuncAddrs (void* expList)
{
	const Int32 result = GetExportedFuncAddrs (expList);
	auto* list = static_cast<AddonManager::ApiIdentity::ExpList*> (expList);
	gVersion = list->version;
	gAcapInitialize = list->initialize;
	list->initialize = &AM_Initialize;
	return result;
}

extern "C" Int32 __ACENV_CALL AM_SetImportedFuncAddrs (const void* impList)
{
	std::memcpy (gImpList, impList, sizeof (gImpList));
	gImpListSet = true;
	return SetImportedFuncAddrs (impList);
}

namespace AddonManager {
namespace ApiIdentity {

	bool		Captured ()	{ return gImpListSet && gClient != nullptr; }
	const void*	ImpList ()	{ return gImpList; }
	void*		Client ()	{ return gClient; }
	Int32		Version ()	{ return gVersion; }

} // namespace ApiIdentity
} // namespace AddonManager
