#include "APIEnvir.h"
#include "Lib/Helper/AddonManagerDialog.hpp"

#include "Lib/Execute/CommandLoader.hpp"

#include <commdlg.h>
#include <vector>

#pragma comment (lib, "comdlg32.lib")

namespace AddonManager {
namespace AddonManagerDialog {

namespace {

	enum ControlId { ID_PATH = 100, ID_BROWSE, ID_LOAD, ID_LIST, ID_NAME, ID_STATUS, ID_RUN };

	const wchar_t* const kClassName = L"AddonManagerDialogWnd";
	const int kClientW = 540;
	const int kClientH = 372;

	struct State {
		HWND		hwnd = nullptr;
		HFONT		font = nullptr;
		Selection*	selection = nullptr;
		bool		done = false;
		bool		accepted = false;
	};

	std::wstring GetText (HWND parent, int id)
	{
		HWND h = GetDlgItem (parent, id);
		const int len = GetWindowTextLengthW (h);
		std::wstring s (static_cast<size_t> (len) + 1, L'\0');
		GetWindowTextW (h, &s[0], len + 1);
		s.resize (static_cast<size_t> (len));
		return s;
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

	HWND Make (State& st, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD exStyle = 0)
	{
		HWND c = CreateWindowExW (exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, st.hwnd,
								  reinterpret_cast<HMENU> (static_cast<INT_PTR> (id)), GetModuleHandleW (nullptr), nullptr);
		SendMessageW (c, WM_SETFONT, reinterpret_cast<WPARAM> (st.font), TRUE);
		return c;
	}

	void SetStatus (State& st, const std::wstring& text)
	{
		SetDlgItemTextW (st.hwnd, ID_STATUS, text.c_str ());
	}

	void OnListSelection (State& st);

	// Reads the DLL, fills the list. Keeps `keepName` selected when it is still there.
	void LoadList (State& st, const std::string& keepName)
	{
		SendDlgItemMessageW (st.hwnd, ID_LIST, LB_RESETCONTENT, 0, 0);
		const std::wstring path = GetText (st.hwnd, ID_PATH);
		if (path.empty ()) {
			SetStatus (st, L"Choose a Commands DLL (Browse...).");
			return;
		}

		std::vector<std::string> names;
		std::wstring error;
		if (!AddonManager::CommandLoader::ListCommands (path, names, error)) {
			SetStatus (st, error);
			return;
		}
		for (const std::string& n : names)
			SendDlgItemMessageW (st.hwnd, ID_LIST, LB_ADDSTRING, 0, reinterpret_cast<LPARAM> (Widen (n).c_str ()));

		if (names.empty ()) {
			SetStatus (st, L"Loaded. The DLL has no ListCommands export: type the command name.");
			return;
		}
		SetStatus (st, std::to_wstring (names.size ()) + L" command(s). Double-click one to run it.");
		const LRESULT idx = SendDlgItemMessageW (st.hwnd, ID_LIST, LB_FINDSTRINGEXACT, static_cast<WPARAM> (-1), reinterpret_cast<LPARAM> (Widen (keepName).c_str ()));
		// the remembered command, else the first one (so Enter works from the keyboard)
		SendDlgItemMessageW (st.hwnd, ID_LIST, LB_SETCURSEL, idx != LB_ERR ? static_cast<WPARAM> (idx) : 0, 0);
		OnListSelection (st);
	}

	void Browse (State& st)
	{
		wchar_t file[MAX_PATH] = {};
		const std::wstring current = GetText (st.hwnd, ID_PATH);
		wcsncpy_s (file, current.c_str (), _TRUNCATE);

		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof (ofn);
		ofn.hwndOwner = st.hwnd;
		ofn.lpstrFilter = L"Commands DLL (*.dll)\0*.dll\0All files\0*.*\0";
		ofn.lpstrFile = file;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrTitle = L"Select a Commands DLL";
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameW (&ofn)) {
			SetDlgItemTextW (st.hwnd, ID_PATH, file);
			LoadList (st, "");
		}
	}

	void OnListSelection (State& st)
	{
		const LRESULT idx = SendDlgItemMessageW (st.hwnd, ID_LIST, LB_GETCURSEL, 0, 0);
		if (idx == LB_ERR)
			return;
		wchar_t buf[256] = {};
		SendDlgItemMessageW (st.hwnd, ID_LIST, LB_GETTEXT, static_cast<WPARAM> (idx), reinterpret_cast<LPARAM> (buf));
		SetDlgItemTextW (st.hwnd, ID_NAME, buf);
	}

	void Accept (State& st)
	{
		const std::wstring path = GetText (st.hwnd, ID_PATH);
		const std::wstring name = GetText (st.hwnd, ID_NAME);
		if (path.empty () || name.empty ()) {
			MessageBoxW (st.hwnd, L"Choose a DLL and a command first.", L"Add-in Manager", MB_OK | MB_ICONINFORMATION);
			return;
		}
		st.selection->dllPath = path;
		st.selection->toolName = Narrow (name);
		st.accepted = true;
		st.done = true;
	}

	void CreateControls (State& st)
	{
		const DWORD tab = WS_TABSTOP;
		Make (st, L"STATIC", L"Commands DLL (loaded from a temporary copy: you can rebuild it while Archicad runs):", SS_LEFT, 16, 14, 508, 18, -1);
		Make (st, L"EDIT", st.selection->dllPath.c_str (), tab | ES_AUTOHSCROLL, 16, 36, 340, 24, ID_PATH, WS_EX_CLIENTEDGE);
		Make (st, L"BUTTON", L"Browse...", BS_PUSHBUTTON | tab, 362, 35, 80, 26, ID_BROWSE);
		Make (st, L"BUTTON", L"Reload", BS_PUSHBUTTON | tab, 448, 35, 76, 26, ID_LOAD);

		Make (st, L"STATIC", L"Commands:", SS_LEFT, 16, 72, 200, 18, -1);
		Make (st, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | tab, 16, 92, 508, 160, ID_LIST);

		Make (st, L"STATIC", L"Command to run:", SS_LEFT, 16, 264, 200, 18, -1);
		Make (st, L"EDIT", Widen (st.selection->toolName).c_str (), tab | ES_AUTOHSCROLL, 16, 284, 508, 24, ID_NAME, WS_EX_CLIENTEDGE);

		Make (st, L"STATIC", L"", SS_LEFT, 16, 322, 300, 36, ID_STATUS);
		Make (st, L"BUTTON", L"Run", BS_DEFPUSHBUTTON | tab, 338, 328, 90, 28, ID_RUN);
		Make (st, L"BUTTON", L"Close", BS_PUSHBUTTON | tab, 434, 328, 90, 28, IDCANCEL);
	}

	LRESULT CALLBACK WndProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		State* st = reinterpret_cast<State*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));
		switch (msg) {
			case WM_NCCREATE:
				SetWindowLongPtrW (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (reinterpret_cast<const CREATESTRUCTW*> (lp)->lpCreateParams));
				break;
			case WM_COMMAND:
				if (st == nullptr)
					return 0;
				switch (LOWORD (wp)) {
					case IDCANCEL:	if (HIWORD (wp) == BN_CLICKED) st->done = true;	break;
					case ID_RUN:	if (HIWORD (wp) == BN_CLICKED) Accept (*st);	break;
					case ID_BROWSE:	if (HIWORD (wp) == BN_CLICKED) Browse (*st);	break;
					case ID_LOAD:	if (HIWORD (wp) == BN_CLICKED) LoadList (*st, Narrow (GetText (st->hwnd, ID_NAME)));	break;
					case ID_LIST:
						if (HIWORD (wp) == LBN_SELCHANGE)
							OnListSelection (*st);
						else if (HIWORD (wp) == LBN_DBLCLK) {
							OnListSelection (*st);
							Accept (*st);
						}
						break;
				}
				return 0;
			case WM_CLOSE:
				if (st != nullptr)
					st->done = true;
				return 0;
		}
		return DefWindowProcW (hwnd, msg, wp, lp);
	}

	HFONT CreateUiFont ()
	{
		NONCLIENTMETRICSW ncm = {};
		ncm.cbSize = sizeof (ncm);
		if (SystemParametersInfoW (SPI_GETNONCLIENTMETRICS, sizeof (ncm), &ncm, 0))
			return CreateFontIndirectW (&ncm.lfMessageFont);
		return static_cast<HFONT> (GetStockObject (DEFAULT_GUI_FONT));
	}

} // namespace


bool Show (Selection& selection)
{
	State st;
	st.selection = &selection;
	st.font = CreateUiFont ();
	HWND owner = GetActiveWindow ();
	if (owner == nullptr)
		owner = GetForegroundWindow ();

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof (wc);
	wc.lpfnWndProc = &WndProc;
	wc.hInstance = GetModuleHandleW (nullptr);
	wc.hCursor = LoadCursorW (nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH> (COLOR_BTNFACE + 1);
	wc.lpszClassName = kClassName;
	RegisterClassExW (&wc);

	const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
	RECT rc = { 0, 0, kClientW, kClientH };
	AdjustWindowRectEx (&rc, style, FALSE, WS_EX_DLGMODALFRAME);
	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;

	RECT area = {};
	if (owner == nullptr || !GetWindowRect (owner, &area))
		SystemParametersInfoW (SPI_GETWORKAREA, 0, &area, 0);
	const int x = area.left + ((area.right - area.left) - w) / 2;
	const int y = area.top + ((area.bottom - area.top) - h) / 2;

	st.hwnd = CreateWindowExW (WS_EX_DLGMODALFRAME, kClassName, L"Add-in Manager", style, x, y, w, h, owner, nullptr, wc.hInstance, &st);
	if (st.hwnd == nullptr) {
		UnregisterClassW (kClassName, wc.hInstance);
		return false;
	}

	CreateControls (st);
	LoadList (st, selection.toolName);
	if (owner != nullptr)
		EnableWindow (owner, FALSE);
	ShowWindow (st.hwnd, SW_SHOW);
	SetFocus (GetDlgItem (st.hwnd, ID_LIST));

	// Own loop: PostQuitMessage would end the message loop of Archicad itself
	MSG msg;
	while (!st.done && GetMessageW (&msg, nullptr, 0, 0) > 0) {
		if (!IsDialogMessageW (st.hwnd, &msg)) {
			TranslateMessage (&msg);
			DispatchMessageW (&msg);
		}
	}

	if (owner != nullptr) {
		EnableWindow (owner, TRUE);
		SetForegroundWindow (owner);
	}
	DestroyWindow (st.hwnd);
	UnregisterClassW (kClassName, wc.hInstance);
	if (st.font != nullptr)
		DeleteObject (st.font);
	return st.accepted;
}

} // namespace AddonManagerDialog
} // namespace AddonManager
