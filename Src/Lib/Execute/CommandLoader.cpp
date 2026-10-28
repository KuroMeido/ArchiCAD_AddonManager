#include "APIEnvir.h"
#include "ACAPinc.h"
#include "CommandsApi.hpp"
#include "Lib/Execute/CommandLoader.hpp"
#include "Lib/Helper/ArchicadHostHelper.hpp"

#include <string>

namespace AddonManager {
namespace CommandLoader {

namespace {

	void Report (const GS::UniString& text)
	{
		ACAPI_WriteReport ("%s", true, text.ToCStr (CC_UTF8).Get ());
	}

	GS::UniString ToUni (const std::wstring& s)
	{
		return GS::UniString (reinterpret_cast<const GS::uchar_t*> (s.c_str ()));
	}

	// A DLL loaded from a temporary copy; the destructor unloads and deletes the copy.
	class TempModule {
	public:
		TempModule () = default;
		TempModule (const TempModule&) = delete;
		TempModule& operator= (const TempModule&) = delete;
		~TempModule ()
		{
			if (mod != nullptr)
				FreeLibrary (mod);
			if (!tmpDll.empty ())
				DeleteFileW (tmpDll.c_str ());
		}

		// Returns 0 or a message in `error`.
		bool Load (const std::wstring& dllPath, std::wstring& error)
		{
			wchar_t tmpDir[MAX_PATH] = {};
			GetTempPathW (MAX_PATH, tmpDir);
			tmpDll = std::wstring (tmpDir) + L"SheetXL_" + std::to_wstring (GetTickCount64 ()) + L".dll";

			if (!CopyFileW (dllPath.c_str (), tmpDll.c_str (), FALSE)) {
				tmpDll.clear ();
				error = L"DLL not found:\n" + dllPath;
				return false;
			}
			mod = LoadLibraryW (tmpDll.c_str ());
			if (mod == nullptr) {
				error = L"Cannot load the DLL (Windows error " + std::to_wstring (GetLastError ()) + L").";
				return false;
			}
			return true;
		}

		FARPROC Proc (const char* name) const { return GetProcAddress (mod, name); }

	private:
		HMODULE		 mod = nullptr;
		std::wstring tmpDll;
	};

} // namespace


int RunFrom (const std::wstring& dllPath, const char* toolName)
{
	TempModule dll;
	std::wstring error;
	if (!dll.Load (dllPath, error)) {
		Report (ToUni (error));
		return APIERR_GENERAL;
	}

	auto runCommand = reinterpret_cast<SheetXL_RunCommandFn> (dll.Proc (SHEETXL_EXPORT_RUNCOMMAND));
	if (runCommand == nullptr) {
		Report ("The DLL does not export RunCommand.");
		return APIERR_GENERAL;
	}

	// All ACAPI_* calls stay in this module; the DLL reaches them through the host table.
	ArchicadHostHelper::Context* ctx = ArchicadHostHelper::CreateContext ();
	const SheetXL_Host host = ArchicadHostHelper::MakeHost (ctx);
	const int result = runCommand (toolName, &host);
	ArchicadHostHelper::DestroyContext (ctx);
	return result;
}

bool ListCommands (const std::wstring& dllPath, std::vector<std::string>& names, std::wstring& error)
{
	names.clear ();
	TempModule dll;
	if (!dll.Load (dllPath, error))
		return false;
	if (dll.Proc (SHEETXL_EXPORT_RUNCOMMAND) == nullptr) {
		error = L"The DLL does not export RunCommand: it is not a Commands DLL.";
		return false;
	}

	auto list = reinterpret_cast<SheetXL_ListCommandsFn> (dll.Proc (SHEETXL_EXPORT_LISTCOMMANDS));
	if (list == nullptr)
		return true;	// runnable, but the names must be typed

	std::string buf (static_cast<size_t> (list (nullptr, 0)) + 1, '\0');
	list (&buf[0], static_cast<int> (buf.size ()));
	buf.resize (std::char_traits<char>::length (buf.c_str ()));

	size_t start = 0;
	while (start <= buf.size ()) {
		size_t end = buf.find ('\n', start);
		if (end == std::string::npos)
			end = buf.size ();
		if (end > start)
			names.push_back (buf.substr (start, end - start));
		start = end + 1;
	}
	return true;
}

} // namespace CommandLoader
} // namespace AddonManager
