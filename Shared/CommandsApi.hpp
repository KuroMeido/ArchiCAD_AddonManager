#ifndef COMMANDSAPI_HPP
#define COMMANDSAPI_HPP

#include <cstddef>	// wchar_t is a typedef here (/Zc:wchar_t- required by the Archicad DevKit)

// Contract between the UI add-on (.apx) and the hot-reloadable Commands DLL.
// Plain C types only: both sides are built separately.
// ArchiCAD_AddonManager\Shared holds its own copy of this contract (it does not depend on the pyArchiCAD
// solutions); a Commands DLL is compatible as long as it builds against the same struct layout.
//
// Why a "host": every ACAPI_* call must come from the module Archicad registered as the add-on (it carries
// the add-on identity). The Commands DLL is only a plain DLL, so all Archicad calls are made by the UI add-on
// and the Commands DLL asks for them through this table. Commands keep the logic, the UI keeps the API.

struct SheetXL_Host {
	unsigned int size;		// sizeof (SheetXL_Host), lets the DLL detect an older/newer UI
	void*		 ctx;		// passed back as the first argument of every callback

	// Message in Archicad's warning dialog.
	void (__cdecl *ShowMessage)    (void* ctx, const wchar_t* text);
	// Diagnostics of the last Archicad lookups (API error codes), for explaining empty results.
	int  (__cdecl *GetDiagnostics) (void* ctx, wchar_t* buf, int cap);

	// Master layouts of the project, subsets and existing layout IDs of the Layout Book (snapshot taken
	// by the UI when the command starts). Names are UTF-16, zero terminated; return = length or -1.
	int  (__cdecl *GetMasterCount)   (void* ctx);
	int  (__cdecl *GetMasterName)    (void* ctx, int index, wchar_t* buf, int cap);
	int  (__cdecl *GetSubsetCount)   (void* ctx);
	int  (__cdecl *GetSubsetName)    (void* ctx, int index, wchar_t* buf, int cap);
	int  (__cdecl *GetLayoutIdCount) (void* ctx);
	int  (__cdecl *GetLayoutId)      (void* ctx, int index, wchar_t* buf, int cap);

	// Creates one layout. masterIndex: index from GetMasterName; subsetIndex: index from GetSubsetName or -1
	// for the layout root. Returns the Archicad error code (0 = created). Not undoable.
	int  (__cdecl *CreateLayout) (void* ctx, const wchar_t* name, const wchar_t* number, int masterIndex, int subsetIndex);

	// ---- appended later: an older UI reports a smaller `size`, check it before using these ----

	// Every layout of the Layout Book in tree order (snapshot taken when the command starts).
	// GetLayoutInfo fills the ID, the name and the name of the parent subset ("" = layout root); return = 0 or -1.
	int  (__cdecl *GetLayoutCount) (void* ctx);
	int  (__cdecl *GetLayoutInfo)  (void* ctx, int index, wchar_t* id, int idCap, wchar_t* name, int nameCap, wchar_t* subset, int subsetCap);

	// Sets the name and the ID of layout `index` (the full new values). Returns the Archicad error code (0 = done). Not undoable.
	int  (__cdecl *RenameLayout) (void* ctx, int index, const wchar_t* newName, const wchar_t* newId);

	// ---- Room Layout (Room panel) ----

	// Zone selected on the plan: name, number, bounding box in m (minX, minY, maxX, maxY) and story index.
	// Returns 0 = found, -1 = no zone selected, else an Archicad error code.
	int  (__cdecl *GetSelectedZone) (void* ctx, wchar_t* name, int nameCap, wchar_t* number, int numberCap, double* bbox, int* floorIndex);
	// Interior elevation along a polyline of pointCount points (xy = x0,y0,x1,y1...; pointCount-1 views), depth in m.
	int  (__cdecl *CreateInteriorElevation) (void* ctx, const wchar_t* name, const wchar_t* id, const double* xy, int pointCount, double depth, int floorIndex);
	// New folder in the View Map; folderHandle identifies it in the two calls below.
	int  (__cdecl *CreateViewMapFolder) (void* ctx, const wchar_t* name, int* folderHandle);
	// Plan view of a story saved in the folder; zoom = xMin, yMin, xMax, yMax in m (null = keep the view's zoom).
	int  (__cdecl *AddPlanView) (void* ctx, int folderHandle, int floorIndex, const wchar_t* viewName, const double* zoom);
	// Saves the views of the interior elevation created by CreateInteriorElevation in the folder (<prefix>_01, _02...).
	// viewParams = { margin, height } in m: every view is cropped to its own wall (null = keep the view's zoom).
	int  (__cdecl *AddInteriorElevationViews) (void* ctx, int folderHandle, const wchar_t* elevationName, const wchar_t* viewNamePrefix, const double* viewParams, int* count);
	// New Layout Book subset (at the root); subsetIndex is valid for CreateLayout (the snapshot is refreshed).
	int  (__cdecl *CreateLayoutSubset) (void* ctx, const wchar_t* name, int* subsetIndex);
	// Places a saved view on the layout `layoutName` as a linked drawing at 1:targetScale. viewKind: 0 = plan view, 1 = elevation view;
	// viewIndex = creation order. x, y = lower-left corner on the layout in m; width / height return the drawing size in m.
	int  (__cdecl *PlaceViewOnLayout) (void* ctx, const wchar_t* layoutName, int viewKind, int viewIndex, double x, double y, double targetScale, double* width, double* height);
	// Refreshes the content of every drawing placed so far by PlaceViewOnLayout (their bounds go to the diagnostics).
	int  (__cdecl *UpdatePlacedDrawings) (void* ctx);
	// Outline of the selected zone as x0,y0,x1,y1... (without the closing point); returns the number of points (0 = none).
	int  (__cdecl *GetZoneOutline) (void* ctx, double* xy, int maxPoints);

	// ---- Align Views (Sheet panel) ----

	// Drawings of layout `layoutIndex` (index into the GetLayoutInfo list). Returns the count, -1 on error.
	int  (__cdecl *GetDrawingCount) (void* ctx, int layoutIndex);
	// name = source view name, key = source view GUID; v (8 values) = pos.x, pos.y (m), ratio, angle (rad), anchor id, modelOffset.x, modelOffset.y, 1 if the view is a floor plan.
	int  (__cdecl *GetDrawing) (void* ctx, int layoutIndex, int drawingIndex, wchar_t* name, int nameCap, wchar_t* key, int keyCap, double* v);
	// flags: 1 = position (with anchor and model offset), 2 = scale ratio, 4 = rotation; `v` laid out as in GetDrawing.
	int  (__cdecl *SetDrawing) (void* ctx, int layoutIndex, int drawingIndex, const double* v, unsigned int flags);
};

// Smallest `size` a UI add-on may report: the table up to CreateLayout (what the first command needs).
#define SHEETXL_HOST_BASE_SIZE	(offsetof (SheetXL_Host, CreateLayout) + sizeof (void*))
// True when the UI add-on that built `host` already has the callback `member`.
#define SHEETXL_HOST_HAS(host, member)	((host)->size >= offsetof (SheetXL_Host, member) + sizeof ((host)->member) && (host)->member != nullptr)

// int RunCommand (const char* toolName, const SheetXL_Host* host)
//   Runs the command registered under `toolName` (e.g. "Sheet_SheetFromExcel").
//   Returns 0 on success, nonzero on failure. The command shows its own dialogs and reports.
#define SHEETXL_EXPORT_RUNCOMMAND	"RunCommand"
typedef int (__cdecl *SheetXL_RunCommandFn) (const char* toolName, const SheetXL_Host* host);

// int ListCommands (char* buf, int cap)   (optional export, used by the Add-in Manager)
//   Writes the tool names the DLL can run, ASCII separated by '\n', zero terminated. Returns the length needed
//   (without the terminator), so a result >= cap means the buffer was too small. A DLL without it can still be run by typing a name.
#define SHEETXL_EXPORT_LISTCOMMANDS	"ListCommands"
typedef int (__cdecl *SheetXL_ListCommandsFn) (char* buf, int cap);

#endif
