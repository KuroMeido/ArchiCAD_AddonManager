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
	const int kClientW = 620;
	const int kClientH = 392;

	struct State {
		HWND		hwnd = nullptr;
		HFONT		font = nullptr;
		Selection*	selection = nullptr;
		std::vector<CommandLoader::Command>	commands;	// same order as the list box
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

	// Key of the selected command (kept selected across a Reload), else the remembered one.
	std::wstring SelectedKey (const State& st)
	{
		const LRESULT idx = SendDlgItemMessageW (st.hwnd, ID_LIST, LB_GETCURSEL, 0, 0);
		if (idx != LB_ERR && static_cast<size_t> (idx) < st.commands.size ())
			return st.commands[static_cast<size_t> (idx)].key;
		return st.selection->command;
	}

	// Loads a fresh copy of the add-on / DLL, fills the list. Keeps `keepKey` selected when it is still there.
	void LoadList (State& st, const std::wstring& keepKey)
	{
		SendDlgItemMessageW (st.hwnd, ID_LIST, LB_RESETCONTENT, 0, 0);
		st.commands.clear ();
		const std::wstring path = GetText (st.hwnd, ID_PATH);
		if (path.empty ()) {
			SetStatus (st, L"Choose an add-on (.apx) or a Commands DLL (Browse...).");
			return;
		}

		CommandLoader::ModuleInfo info;
		std::wstring error;
		if (!CommandLoader::Inspect (path, info, error)) {
			SetStatus (st, error);
			return;
		}
		st.commands = info.commands;
		for (const CommandLoader::Command& c : st.commands)
			SendDlgItemMessageW (st.hwnd, ID_LIST, LB_ADDSTRING, 0, reinterpret_cast<LPARAM> (c.label.c_str ()));

		const std::wstring what = info.name.empty () ? info.kind : info.name + L" (" + info.kind + L")";
		if (st.commands.empty ()) {
			SetStatus (st, what + L": no command found. Type a tool name of the DLL.");
			return;
		}
		SetStatus (st, what + L": " + std::to_wstring (st.commands.size ()) + L" command(s). Double-click one to run it.");

		// the remembered command, else the first one (so Enter works from the keyboard)
		size_t idx = 0;
		for (size_t i = 0; i < st.commands.size (); ++i)
			if (st.commands[i].key == keepKey)
				idx = i;
		SendDlgItemMessageW (st.hwnd, ID_LIST, LB_SETCURSEL, static_cast<WPARAM> (idx), 0);
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
		ofn.lpstrFilter = L"Add-ons and Commands DLLs (*.apx;*.dll)\0*.apx;*.dll\0All files\0*.*\0";
		ofn.lpstrFile = file;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrTitle = L"Select an add-on or a Commands DLL";
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameW (&ofn)) {
			SetDlgItemTextW (st.hwnd, ID_PATH, file);
			LoadList (st, L"");
		}
	}

	void OnListSelection (State& st)
	{
		const LRESULT idx = SendDlgItemMessageW (st.hwnd, ID_LIST, LB_GETCURSEL, 0, 0);
		if (idx == LB_ERR || static_cast<size_t> (idx) >= st.commands.size ())
			return;
		SetDlgItemTextW (st.hwnd, ID_NAME, st.commands[static_cast<size_t> (idx)].label.c_str ());
	}

	void Accept (State& st)
	{
		const std::wstring path = GetText (st.hwnd, ID_PATH);
		const std::wstring text = GetText (st.hwnd, ID_NAME);
		if (path.empty () || text.empty ()) {
			MessageBoxW (st.hwnd, L"Choose an add-on or a DLL, and a command first.", L"Add-in Manager", MB_OK | MB_ICONINFORMATION);
			return;
		}
		// a command of the list, else a tool name typed for a Commands DLL
		std::wstring key = L"run:" + text;
		for (const CommandLoader::Command& c : st.commands)
			if (c.label == text)
				key = c.key;
		st.selection->path = path;
		st.selection->command = key;
		st.accepted = true;
		st.done = true;
	}

	void CreateControls (State& st)
	{
		const DWORD tab = WS_TABSTOP;
		Make (st, L"STATIC", L"Add-on (.apx) or Commands DLL, run from a temporary copy: rebuild it and Reload, no Archicad restart.", SS_LEFT, 16, 14, 588, 18, -1);
		Make (st, L"EDIT", st.selection->path.c_str (), tab | ES_AUTOHSCROLL, 16, 36, 420, 24, ID_PATH, WS_EX_CLIENTEDGE);
		Make (st, L"BUTTON", L"Browse...", BS_PUSHBUTTON | tab, 442, 35, 80, 26, ID_BROWSE);
		Make (st, L"BUTTON", L"Reload", BS_PUSHBUTTON | tab, 528, 35, 76, 26, ID_LOAD);

		Make (st, L"STATIC", L"Commands:", SS_LEFT, 16, 72, 200, 18, -1);
		Make (st, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | tab, 16, 92, 588, 180, ID_LIST);

		Make (st, L"STATIC", L"Command to run:", SS_LEFT, 16, 284, 200, 18, -1);
		Make (st, L"EDIT", L"", tab | ES_AUTOHSCROLL, 16, 304, 588, 24, ID_NAME, WS_EX_CLIENTEDGE);

		Make (st, L"STATIC", L"", SS_LEFT, 16, 342, 390, 40, ID_STATUS);
		Make (st, L"BUTTON", L"Run", BS_DEFPUSHBUTTON | tab, 418, 348, 90, 28, ID_RUN);
		Make (st, L"BUTTON", L"Close", BS_PUSHBUTTON | tab, 514, 348, 90, 28, IDCANCEL);
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
					case ID_LOAD:	if (HIWORD (wp) == BN_CLICKED) LoadList (*st, SelectedKey (*st));	break;
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
	LoadList (st, selection.command);
	if (owner != nullptr)
		EnableWindow (owner, FALSE);
	ShowWindow (st.hwnd, SW_SHOW);
	SetFocus (GetDlgItem (st.hwnd, ID_LIST));

	// Keys still queued from the menu choice (the Enter that picked "Manual Mode") would press Run at once
	MSG msg;
	while (PeekMessageW (&msg, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
		;

	// Own loop: PostQuitMessage would end the message loop of Archicad itself
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
