#include "APIEnvir.h"
#include "ACAPinc.h"
#include "CommandsApi.hpp"
#include "Lib/Execute/ApiIdentity.hpp"
#include "Lib/Execute/CommandLoader.hpp"
#include "Lib/Execute/ArchicadHostHelper.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace AddonManager {
namespace CommandLoader {

namespace {

	const std::wstring kMenuKey = L"menu:";
	const std::wstring kRunKey  = L"run:";

	void Report (const GS::UniString& text)
	{
		ACAPI_WriteReport ("%s", true, text.ToCStr (CC_UTF8).Get ());
	}

	GS::UniString ToUni (const std::wstring& s)
	{
		return GS::UniString (reinterpret_cast<const GS::uchar_t*> (s.c_str ()));
	}

	std::wstring FromUni (const GS::UniString& s)
	{
		return std::wstring (reinterpret_cast<const wchar_t*> (s.ToUStr ().Get ()));
	}

	// Tool names are ASCII. (wchar_t is an unsigned short typedef in this build: explicit casts.)
	std::wstring Widen (const std::string& s)
	{
		std::wstring w;
		for (char c : s)
			w += static_cast<wchar_t> (static_cast<unsigned char> (c));
		return w;
	}

	std::string Narrow (const std::wstring& s)
	{
		std::string n;
		for (wchar_t c : s)
			n += static_cast<char> (c);
		return n;
	}

	bool StartsWith (const std::wstring& s, const std::wstring& prefix)
	{
		return s.compare (0, prefix.size (), prefix) == 0;
	}

	bool EndsWithNoCase (const std::wstring& s, const wchar_t* suffix)
	{
		const size_t n = std::wcslen (suffix);
		return s.size () >= n && _wcsicmp (s.c_str () + s.size () - n, suffix) == 0;
	}

	// Menu text without the control codes ("Item...^ES^EE^EL" -> "Item...").
	std::wstring MenuText (const std::wstring& s)
	{
		std::wstring t = s.substr (0, s.find (L'^'));
		while (!t.empty () && t.back () == L' ')
			t.pop_back ();
		return t;
	}

	// What the ACAPI_MenuItem_RegisterMenu stub of ACAP_STAT passes to Archicad.
	struct RegisterMenuArgs {
		short	menuStrResID;
		short	promptStrResID;
		Int32	menuPosCode;
		UInt32	menuFlags;
	};

	class LoadedModule;
	LoadedModule* gLoading = nullptr;	// module whose code is running: target of the recording functions below

	// A module loaded from a temporary copy. Unload: FreeData of the add-on, FreeLibrary, copies deleted.
	class LoadedModule {
	public:
		LoadedModule () = default;
		LoadedModule (const LoadedModule&) = delete;
		LoadedModule& operator= (const LoadedModule&) = delete;
		~LoadedModule () { Unload (); }

		bool				Load (const std::wstring& path, std::wstring& error);
		int					Run (const std::wstring& key);
		const ModuleInfo&	Info () const { return info; }

		std::map<short, APIMenuCommandProc*>	handlers;	// menu handlers the add-on tried to install
		std::map<short, Int32>					menuPos;	// menu position code of each registered menu

	private:
		bool	CopyToTemp (const std::wstring& path, std::wstring& copy, std::wstring& error);
		bool	InitAddOn (std::wstring& error);
		void	ListMenuCommands ();
		void	ListRunCommands ();
		void	Unload ();

		HMODULE						mod = nullptr;
		std::wstring				tmpDir;
		ApiIdentity::ExpList		exp = {};
		bool						initialized = false;
		SheetXL_RunCommandFn		runCommand = nullptr;
		ModuleInfo					info;
	};

	struct LoadingScope {
		explicit LoadingScope (LoadedModule* m) { gLoading = m; }
		~LoadingScope () { gLoading = nullptr; }
	};

	// Slots of the Archicad table handed to the module (same signatures as the ACAP_STAT stubs call them).
	GSErrCode __ACENV_CALL IgnoreCall ()
	{
		return NoError;
	}

	GSErrCode __ACENV_CALL RecordRegisterMenu (const RegisterMenuArgs* args)
	{
		if (gLoading != nullptr && args != nullptr)
			gLoading->menuPos[args->menuStrResID] = args->menuPosCode;
		return NoError;
	}

	GSErrCode __ACENV_CALL RecordMenuHandler (void* /*client*/, short menuStrResID, APIMenuCommandProc* handler)
	{
		if (gLoading != nullptr)
			gLoading->handlers[menuStrResID] = handler;
		return NoError;
	}

	// What stays behind when the folder of the add-on is copied: other add-ons and build leftovers.
	bool SkipFile (const std::wstring& name)
	{
		for (const wchar_t* ext : { L".apx", L".pdb", L".ilk", L".lib", L".exp", L".obj", L".iobj", L".ipdb" })
			if (EndsWithNoCase (name, ext))
				return true;
		return false;
	}

	bool SkipDir (const std::wstring& name)
	{
		for (const wchar_t* skip : { L".", L"..", L"build", L".vs", L"CMakeFiles" })
			if (_wcsicmp (name.c_str (), skip) == 0)
				return true;
		return false;
	}

	// Copies the tree `src` into `dst`. False (and `error`) when it passes kMaxFolderBytes or a copy fails.
	constexpr ULONGLONG kMaxFolderBytes = 300ull * 1024 * 1024;
	bool CopyTree (const std::wstring& src, const std::wstring& dst, ULONGLONG& bytes, std::wstring& error)
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE find = FindFirstFileW ((src + L"\\*").c_str (), &fd);
		if (find == INVALID_HANDLE_VALUE)
			return true;
		bool ok = true;
		do {
			const std::wstring name = fd.cFileName;
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
				if (SkipDir (name))
					continue;
				CreateDirectoryW ((dst + L"\\" + name).c_str (), nullptr);
				ok = CopyTree (src + L"\\" + name, dst + L"\\" + name, bytes, error);
			} else if (!SkipFile (name)) {
				bytes += (static_cast<ULONGLONG> (fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
				if (bytes > kMaxFolderBytes) {
					error = L"The folder of the add-on holds more than 300 MB: put the add-on and its files in a folder of their own.";
					ok = false;
				} else if (!CopyFileW ((src + L"\\" + name).c_str (), (dst + L"\\" + name).c_str (), FALSE)) {
					error = L"Cannot copy " + src + L"\\" + name + L" (Windows error " + std::to_wstring (GetLastError ()) + L").";
					ok = false;
				}
			}
		} while (ok && FindNextFileW (find, &fd));
		FindClose (find);
		return ok;
	}

	// Deletes a folder and everything in it (a file still in use stays, harmlessly).
	void DeleteTree (const std::wstring& dir)
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE find = FindFirstFileW ((dir + L"\\*").c_str (), &fd);
		if (find != INVALID_HANDLE_VALUE) {
			do {
				const std::wstring name = fd.cFileName;
				if (name == L"." || name == L"..")
					continue;
				if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					DeleteTree (dir + L"\\" + name);
				else
					DeleteFileW ((dir + L"\\" + name).c_str ());
			} while (FindNextFileW (find, &fd));
			FindClose (find);
		}
		RemoveDirectoryW (dir.c_str ());
	}

	// The add-on with the whole content of its folder (DLLs, data files, subfolders), since an add-on finds
	// its own files next to itself.
	bool LoadedModule::CopyToTemp (const std::wstring& path, std::wstring& copy, std::wstring& error)
	{
		wchar_t tmp[MAX_PATH] = {};
		GetTempPathW (MAX_PATH, tmp);
		const std::wstring root = std::wstring (tmp) + L"AddonManager";
		CreateDirectoryW (root.c_str (), nullptr);
		tmpDir = root + L"\\" + std::to_wstring (GetTickCount64 ());
		CreateDirectoryW (tmpDir.c_str (), nullptr);

		const size_t slash = path.find_last_of (L"\\/");
		const std::wstring dir = slash == std::wstring::npos ? L"." : path.substr (0, slash);
		copy = tmpDir + L"\\" + path.substr (slash == std::wstring::npos ? 0 : slash + 1);
		if (!CopyFileW (path.c_str (), copy.c_str (), FALSE)) {
			error = L"File not found:\n" + path;
			return false;
		}
		ULONGLONG bytes = 0;
		return CopyTree (dir, tmpDir, bytes, error);
	}

	bool LoadedModule::Load (const std::wstring& path, std::wstring& error)
	{
		std::wstring copy;
		if (!CopyToTemp (path, copy, error))
			return false;

		mod = LoadLibraryExW (copy.c_str (), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (mod == nullptr) {
			error = L"Cannot load the file (Windows error " + std::to_wstring (GetLastError ()) + L").";
			return false;
		}

		runCommand = reinterpret_cast<SheetXL_RunCommandFn> (GetProcAddress (mod, SHEETXL_EXPORT_RUNCOMMAND));
		const bool addOn = GetProcAddress (mod, "GetExportedFuncAddrs") != nullptr && GetProcAddress (mod, "SetImportedFuncAddrs") != nullptr;
		if (!addOn && runCommand == nullptr) {
			error = L"Neither an Archicad add-on (no GetExportedFuncAddrs) nor a Commands DLL (no RunCommand).";
			return false;
		}
		if (addOn && !InitAddOn (error))
			return false;

		info.kind = !addOn ? L"Commands DLL" : (runCommand != nullptr ? L"Commands DLL with Archicad API" : L"Add-on");
		ListMenuCommands ();
		ListRunCommands ();
		return true;
	}

	bool LoadedModule::InitAddOn (std::wstring& error)
	{
		if (!ApiIdentity::Captured ()) {
			error = L"The manager did not receive its Archicad identity: add-ons cannot run.";
			return false;
		}
		using GetExpFn = Int32 (__ACENV_CALL *) (void*);
		using SetImpFn = Int32 (__ACENV_CALL *) (const void*);
		const auto getExp = reinterpret_cast<GetExpFn> (GetProcAddress (mod, "GetExportedFuncAddrs"));
		const auto setImp = reinterpret_cast<SetImpFn> (GetProcAddress (mod, "SetImportedFuncAddrs"));

		getExp (&exp);
		if (exp.version != ApiIdentity::Version () || exp.initialize == nullptr) {
			error = L"The add-on was built with another Archicad API version than this Archicad.";
			return false;
		}

		LoadingScope scope (this);
		std::vector<void*> table (ApiIdentity::kImpListSize / sizeof (void*), reinterpret_cast<void*> (&IgnoreCall));

		// 1. registration: every Archicad function answers NoError, menu registrations are only recorded
		table[ApiIdentity::kSlotRegisterMenu] = reinterpret_cast<void*> (&RecordRegisterMenu);
		setImp (table.data ());
		API_EnvirParams envir {};
		if (exp.checkEnvironment != nullptr)
			exp.checkEnvironment (&envir);
		info.name = FromUni (envir.addOnInfo.name);
		info.description = FromUni (envir.addOnInfo.description);
		if (exp.registerInterface != nullptr)
			exp.registerInterface (nullptr);

		// 2. the real Archicad table with the manager's client: the add-on calls Archicad as the manager.
		//    Its menu handlers are recorded instead of being installed (the manager runs them).
		std::memcpy (table.data (), ApiIdentity::ImpList (), ApiIdentity::kImpListSize);
		table[ApiIdentity::kSlotInstallMenuHandler] = reinterpret_cast<void*> (&RecordMenuHandler);
		setImp (table.data ());
		const GSErrCode err = exp.initialize (ApiIdentity::Client ());
		initialized = true;
		if (err != NoError) {
			wchar_t code[16] = {};
			swprintf_s (code, L"%08X", static_cast<unsigned int> (err));
			error = std::wstring (L"Initialize of the add-on failed (error ") + code + L").";
			return false;
		}
		return true;
	}

	// A menu resource: [main menu title (MenuCode_UserDef)] [submenu name (when 2+ items)] items...
	void LoadedModule::ListMenuCommands ()
	{
		for (const auto& h : handlers) {
			const short resID = h.first;
			GS::Array<GS::UniString> strings;
			RSGetStringList (&strings, resID, reinterpret_cast<GSResModule> (mod));
			std::vector<std::wstring> rows;
			for (UIndex i = 0; i < strings.GetSize (); ++i)
				rows.push_back (MenuText (FromUni (strings[i])));

			std::wstring prefix;
			size_t first = 0;
			const auto pos = menuPos.find (resID);
			if (pos != menuPos.end () && pos->second == static_cast<Int32> (MenuCode_UserDef) && !rows.empty ()) {
				prefix = rows[0] + L" > ";
				first = 1;
			}
			if (rows.size () > first + 1) {
				prefix += rows[first] + L" > ";
				++first;
			}

			const std::wstring keyBase = kMenuKey + std::to_wstring (resID) + L":";
			if (rows.size () <= first) {
				info.commands.push_back ({ keyBase + L"1", L"Menu " + std::to_wstring (resID) + L", item 1" });
				continue;
			}
			for (size_t i = first; i < rows.size (); ++i)
				info.commands.push_back ({ keyBase + std::to_wstring (i - first + 1), prefix + rows[i] });
		}
	}

	void LoadedModule::ListRunCommands ()
	{
		if (runCommand == nullptr)
			return;
		const auto list = reinterpret_cast<SheetXL_ListCommandsFn> (GetProcAddress (mod, SHEETXL_EXPORT_LISTCOMMANDS));
		if (list == nullptr)
			return;		// runnable, but the names must be typed

		std::string buf (static_cast<size_t> (list (nullptr, 0)) + 1, '\0');
		list (&buf[0], static_cast<int> (buf.size ()));
		buf.resize (std::char_traits<char>::length (buf.c_str ()));

		size_t start = 0;
		while (start <= buf.size ()) {
			size_t end = buf.find ('\n', start);
			if (end == std::string::npos)
				end = buf.size ();
			if (end > start) {
				const std::wstring name = Widen (buf.substr (start, end - start));
				info.commands.push_back ({ kRunKey + name, name });
			}
			start = end + 1;
		}
	}

	int LoadedModule::Run (const std::wstring& key)
	{
		LoadingScope scope (this);
		if (StartsWith (key, kMenuKey)) {
			const size_t colon = key.find (L':', kMenuKey.size ());
			const short resID = static_cast<short> (_wtoi (key.substr (kMenuKey.size ()).c_str ()));
			const short item = colon == std::wstring::npos ? 1 : static_cast<short> (_wtoi (key.substr (colon + 1).c_str ()));
			const auto h = handlers.find (resID);
			if (h == handlers.end () || h->second == nullptr) {
				Report (GS::UniString::Printf ("The add-on has no handler for menu %d any more.", static_cast<int> (resID)));
				return APIERR_GENERAL;
			}
			API_MenuParams params = {};
			params.menuItemRef.menuResID = resID;
			params.menuItemRef.itemIndex = item;
			return h->second (&params);
		}

		const std::wstring name = StartsWith (key, kRunKey) ? key.substr (kRunKey.size ()) : key;
		if (runCommand == nullptr) {
			Report ("The file does not export RunCommand.");
			return APIERR_GENERAL;
		}
		// The DLL reaches Archicad through the host table (or directly, when it links ACAP_STAT).
		ArchicadHostHelper::Context* ctx = ArchicadHostHelper::CreateContext ();
		const SheetXL_Host host = ArchicadHostHelper::MakeHost (ctx);
		const int result = runCommand (Narrow (name).c_str (), &host);
		ArchicadHostHelper::DestroyContext (ctx);
		return result;
	}

	void LoadedModule::Unload ()
	{
		if (initialized && exp.freeData != nullptr) {
			LoadingScope scope (this);
			exp.freeData ();
		}
		initialized = false;
		if (mod != nullptr)
			FreeLibrary (mod);
		mod = nullptr;
		if (!tmpDir.empty ())
			DeleteTree (tmpDir);
		tmpDir.clear ();
	}

	// Raw pointer: released in FreeData, never by a static destructor while Windows unloads the manager.
	LoadedModule* gCurrent = nullptr;

	// Copies left in %TEMP%\AddonManager by a session that did not end normally (Archicad killed or crashed).
	void DeleteStaleCopies ()
	{
		wchar_t tmp[MAX_PATH] = {};
		GetTempPathW (MAX_PATH, tmp);
		const std::wstring root = std::wstring (tmp) + L"AddonManager";
		WIN32_FIND_DATAW dirData = {};
		HANDLE dirs = FindFirstFileW ((root + L"\\*").c_str (), &dirData);
		if (dirs == INVALID_HANDLE_VALUE)
			return;
		do {
			const std::wstring name = dirData.cFileName;
			if ((dirData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || name == L"." || name == L"..")
				continue;
			DeleteTree (root + L"\\" + name);	// a copy still loaded stays, harmlessly
		} while (FindNextFileW (dirs, &dirData));
		FindClose (dirs);
	}

	LoadedModule* LoadFresh (const std::wstring& path, std::wstring& error)
	{
		UnloadAll ();
		static bool staleDeleted = false;
		if (!staleDeleted) {
			DeleteStaleCopies ();
			staleDeleted = true;
		}
		LoadedModule* module = new LoadedModule ();
		if (!module->Load (path, error)) {
			delete module;
			return nullptr;
		}
		gCurrent = module;
		return module;
	}

} // namespace


bool Inspect (const std::wstring& path, ModuleInfo& info, std::wstring& error)
{
	const LoadedModule* module = LoadFresh (path, error);
	if (module == nullptr)
		return false;
	info = module->Info ();
	return true;
}

int Run (const std::wstring& path, const std::wstring& key)
{
	std::wstring error;
	LoadedModule* module = LoadFresh (path, error);
	if (module == nullptr) {
		Report (ToUni (error));
		return APIERR_GENERAL;
	}
	return module->Run (key);
}

void UnloadAll ()
{
	delete gCurrent;
	gCurrent = nullptr;
}

} // namespace CommandLoader
} // namespace AddonManager
