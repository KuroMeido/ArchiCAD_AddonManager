#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Lib/Helper/ArchicadHostHelper.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <cmath>
#include <vector>

namespace AddonManager {
namespace ArchicadHostHelper {

struct Context {
	struct Master {
		GS::UniString		name;
		API_DatabaseUnId	dbId;
	};

	GS::Array<Master>			masters;
	GS::Array<API_NavigatorItem> subsets;
	GS::Array<GS::UniString>	layoutIds;
	struct Layout {
		GS::UniString		id;
		GS::UniString		name;
		GS::UniString		subset;		// parent subset, "" = layout root
		API_DatabaseUnId	dbId;
	};
	GS::Array<Layout>			layouts;
	GS::Array<API_Guid>			viewFolders;		// View Map folders created by the Room Layout command (handle = index)
	GS::Array<API_Guid>			ieBefore;			// interior elevation items that existed before CreateInteriorElevation
	GS::Array<API_Guid>			planViews;			// views saved by AddPlanView, in creation order
	GS::Array<API_Guid>			elevationViews;		// views saved by AddInteriorElevationViews, in creation order
	GS::Array<API_Coord>		iePolyline;			// polyline of the last CreateInteriorElevation
	double						ieBaseLevel = 0.0;	// elevation of the story it was created on
	GS::Array<API_Guid>			drawingCache;		// drawings of layout `drawingCacheLayout` (Align Views)
	int							drawingCacheLayout = -1;
	GS::Array<API_Guid>			placedDrawings;		// drawings created by PlaceViewOnLayout
	API_DatabaseUnId			placedLayoutDb = {};
	GS::Array<API_Guid>			selection;			// selection of the open layout (Copy Views)
	GS::Array<int>				selectionTypes;		// API_ElemTypeID of each selected element
	GS::UniString				diagnostics;
	int							dumpedItems = 0;
};

namespace {

	// Records one layout of the tree. The database info is the source of truth for the ID and name
		// (that is what ACAPI_Database_ModifyDatabase changes); the navigator item is only the fallback.
		void AddLayout (const API_NavigatorItem& item, const GS::UniString& parentName, Context& c)
		{
			Context::Layout layout;
			layout.id = GS::UniString (item.uiId);
			layout.name = GS::UniString (item.uName);
			layout.subset = parentName;
			layout.dbId = item.db.databaseUnId;

			API_DatabaseInfo info = {};
			info.databaseUnId = layout.dbId;
			API_DatabaseUnId idCopy = layout.dbId;
			const GSErrCode err = ACAPI_Window_GetDatabaseInfo (&info, &idCopy);
			if (err == NoError) {
				// info.ref is the full ID with book/subset prefixes ("ABC_A-101"); the Navigator shows the layout's own ID: keep uiId
				layout.name = GS::UniString (info.name);
			} else {
				c.diagnostics += GS::UniString::Printf ("GetDatabaseInfo(layout '%T'): error %d.\n", layout.id.ToPrintf (), err);
			}
			c.layouts.Push (layout);
		}

		// Walks the Layout Book tree: subsets (recursively) and the IDs of the layouts inside them.
	// (ACAPI_Navigator_SearchNavigatorItem finds items by database, it cannot list the tree.)
	void CollectLayoutBookTree (const API_Guid& parentGuid, const GS::UniString& parentName, Context& c)
	{
		API_NavigatorItem parent = {};
		parent.guid = parentGuid;
		parent.mapId = API_LayoutMap;

		GS::Array<API_NavigatorItem> children;
		const GSErrCode err = ACAPI_Navigator_GetNavigatorChildrenItems (&parent, &children);
		if (err != NoError) {
			c.diagnostics += GS::UniString::Printf ("GetNavigatorChildrenItems: error %d.\n", err);
			return;
		}

		for (const API_NavigatorItem& child : children) {
			// Diagnostics: every item Archicad returns (first 60), to see how subsets are really typed
			if (c.dumpedItems < 60) {
				++c.dumpedItems;
				c.diagnostics += GS::UniString::Printf ("  item type=%d name='%T' id='%T'\n", static_cast<int> (child.itemType),
														GS::UniString (child.uName).ToPrintf (), GS::UniString (child.uiId).ToPrintf ());
			}
			if (child.itemType == API_SubSetNavItem) {
				c.subsets.Push (child);
				CollectLayoutBookTree (child.guid, GS::UniString (child.uName), c);
			} else if (child.itemType == API_LayoutNavItem) {
				c.layoutIds.Push (GS::UniString (child.uiId));
				AddLayout (child, parentName, c);
			} else if (child.itemType != API_MasterLayoutNavItem) {
				CollectLayoutBookTree (child.guid, parentName, c);	// folders / books of unknown type: look inside too
			}
		}
	}

	void CollectLayoutBook (Context& c)
	{
		API_NavigatorSet set = {};
		set.mapId = API_LayoutMap;
		const GSErrCode err = ACAPI_Navigator_GetNavigatorSet (&set);
		c.diagnostics += GS::UniString::Printf ("GetNavigatorSet(LayoutMap): error %d.\n", err);
		if (err != NoError)
			return;
		CollectLayoutBookTree (set.rootGuid, GS::UniString (), c);
		c.diagnostics += GS::UniString::Printf ("Layout Book: %u subset(s), %u layout(s).\n", c.subsets.GetSize (), c.layoutIds.GetSize ());
	}

	void CollectMasters (Context& c)
	{
		GS::Array<API_DatabaseUnId> ids;
		GSErrCode err = ACAPI_Database_GetMasterLayoutDatabases (nullptr, &ids);
		c.diagnostics += GS::UniString::Printf ("GetMasterLayoutDatabases: error %d, %u database(s).\n", err, ids.GetSize ());
		if (err != NoError)
			return;

		for (const API_DatabaseUnId& id : ids) {
			API_DatabaseInfo info = {};
			info.databaseUnId = id;
			API_DatabaseUnId idCopy = id;
			err = ACAPI_Window_GetDatabaseInfo (&info, &idCopy);
			c.diagnostics += GS::UniString::Printf ("GetDatabaseInfo: error %d.\n", err);
			if (err != NoError)
				continue;
			c.masters.Push ({ GS::UniString (info.name), id });
		}
	}

	int CopyOut (const GS::UniString& s, wchar_t* buf, int cap)
	{
		if (buf == nullptr || cap <= 0)
			return -1;
		const GS::uchar_t* src = s.ToUStr ().Get ();
		int i = 0;
		for (; src[i] != 0 && i < cap - 1; ++i)
			buf[i] = static_cast<wchar_t> (src[i]);
		buf[i] = 0;
		return i;
	}

	GS::UniString ToUni (const wchar_t* s)
	{
		return s == nullptr ? GS::UniString () : GS::UniString (reinterpret_cast<const GS::uchar_t*> (s));
	}

	Context* C (void* ctx) { return static_cast<Context*> (ctx); }

	// ---- SheetXL_Host callbacks -------------------------------------------------------------

	void __cdecl ShowMessage (void*, const wchar_t* text)
	{
		// "%s": the text itself must never be interpreted as a printf format
		const GS::UniString message = ToUni (text);
		ACAPI_WriteReport ("%s", true, message.ToCStr (CC_UTF8).Get ());

		// the last report is also kept in a text file (the Archicad alert is easy to miss): %TEMP%\pyAtlas_report.txt
		wchar_t path[MAX_PATH] = {};
		if (GetTempPathW (MAX_PATH, path) != 0) {
			const std::wstring file = std::wstring (path) + L"pyAtlas_report.txt";
			HANDLE h = CreateFileW (file.c_str (), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE) {
				const auto utf8Str = message.ToCStr (CC_UTF8);
				const char* utf8 = utf8Str.Get ();
				DWORD written = 0;
				WriteFile (h, utf8, static_cast<DWORD> (std::strlen (utf8)), &written, nullptr);
				CloseHandle (h);
			}
		}
	}

	int __cdecl GetDiagnostics (void* ctx, wchar_t* buf, int cap)	{ return CopyOut (C (ctx)->diagnostics, buf, cap); }

	int __cdecl GetMasterCount (void* ctx)	{ return static_cast<int> (C (ctx)->masters.GetSize ()); }
	int __cdecl GetMasterName (void* ctx, int i, wchar_t* buf, int cap)
	{
		return (i < 0 || i >= static_cast<int> (C (ctx)->masters.GetSize ())) ? -1 : CopyOut (C (ctx)->masters[i].name, buf, cap);
	}

	int __cdecl GetSubsetCount (void* ctx)	{ return static_cast<int> (C (ctx)->subsets.GetSize ()); }
	int __cdecl GetSubsetName (void* ctx, int i, wchar_t* buf, int cap)
	{
		return (i < 0 || i >= static_cast<int> (C (ctx)->subsets.GetSize ())) ? -1 : CopyOut (GS::UniString (C (ctx)->subsets[i].uName), buf, cap);
	}

	int __cdecl GetLayoutIdCount (void* ctx)	{ return static_cast<int> (C (ctx)->layoutIds.GetSize ()); }
	int __cdecl GetLayoutId (void* ctx, int i, wchar_t* buf, int cap)
	{
		return (i < 0 || i >= static_cast<int> (C (ctx)->layoutIds.GetSize ())) ? -1 : CopyOut (C (ctx)->layoutIds[i], buf, cap);
	}

	int __cdecl CreateLayout (void* ctx, const wchar_t* name, const wchar_t* number, int masterIndex, int subsetIndex)
	{
		Context* c = C (ctx);
		if (masterIndex < 0 || masterIndex >= static_cast<int> (c->masters.GetSize ()))
			return APIERR_BADPARS;

		API_DatabaseUnId masterId = c->masters[masterIndex].dbId;

		API_Guid parentGuid = APINULLGuid;
		API_Guid* parent = nullptr;
		if (subsetIndex >= 0 && subsetIndex < static_cast<int> (c->subsets.GetSize ())) {
			parentGuid = c->subsets[subsetIndex].guid;
			parent = &parentGuid;
		}

		// ACAPI_Navigator_CreateLayout is a non-undoable navigator change: it is refused (APIERR_REFUSEDCMD)
		// inside an undo session, so it must not be wrapped in ACAPI_CallUndoableCommand.
		API_LayoutInfo layout = {};
		GS::ucsncpy (layout.layoutName, ToUni (name).ToUStr ().Get (), API_UniLongNameLen - 1);
		layout.customLayoutNumbering = ToUni (number).GetLength () > 0;	// empty number = automatic Book/Subset numbering
		CHTruncate (ToUni (number).ToCStr (CC_UTF8).Get (), layout.customLayoutNumber, sizeof (layout.customLayoutNumber));

		// size/margins belong to the master layout; ignored here
		return ACAPI_Navigator_CreateLayout (&layout, &masterId, parent);
		}

		int __cdecl GetLayoutCount (void* ctx)	{ return static_cast<int> (C (ctx)->layouts.GetSize ()); }
		int __cdecl GetLayoutInfo (void* ctx, int i, wchar_t* id, int idCap, wchar_t* name, int nameCap, wchar_t* subset, int subsetCap)
		{
			const Context* c = C (ctx);
			if (i < 0 || i >= static_cast<int> (c->layouts.GetSize ()))
				return -1;
			const Context::Layout& l = c->layouts[i];
			CopyOut (l.id, id, idCap);
			CopyOut (l.name, name, nameCap);
			CopyOut (l.subset, subset, subsetCap);
			return 0;
		}

		// ---- Room Layout ---------------------------------------------------------------------------

		// Items of a navigator map (depth first), optionally restricted to one type
		void CollectMapItems (API_NavigatorMapID mapId, const API_Guid& parentGuid, GS::Array<API_NavigatorItem>& out)
		{
			API_NavigatorItem parent = {};
			parent.guid = parentGuid;
			parent.mapId = mapId;
			GS::Array<API_NavigatorItem> children;
			if (ACAPI_Navigator_GetNavigatorChildrenItems (&parent, &children) != NoError)
				return;
			for (const API_NavigatorItem& child : children) {
				out.Push (child);
				CollectMapItems (mapId, child.guid, out);
			}
		}

		GS::Array<API_NavigatorItem> AllMapItems (API_NavigatorMapID mapId)
		{
			GS::Array<API_NavigatorItem> items;
			API_NavigatorSet set = {};
			set.mapId = mapId;
			if (ACAPI_Navigator_GetNavigatorSet (&set) == NoError)
				CollectMapItems (mapId, set.rootGuid, items);
			return items;
		}

		bool Contains (const GS::Array<API_Guid>& list, const API_Guid& g)
		{
			for (const API_Guid& x : list)
				if (x == g)
					return true;
			return false;
		}

		void RefreshLayoutBook (Context& c)
		{
			c.subsets.Clear ();
			c.layoutIds.Clear ();
			c.layouts.Clear ();
			CollectLayoutBook (c);
		}

		// Renames a View Map item and, when `zoom` is given, saves that zoom box on the view.
		GSErrCode SetViewNameAndZoom (const API_Guid& guid, const GS::UniString& name, const double* zoom)
		{
			API_NavigatorItem item = {};
			item.guid = guid;
			item.mapId = API_PublicViewMap;
			GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&guid, &item);
			if (err != NoError)
				return err;

			API_NavigatorView view = {};
			err = ACAPI_Navigator_GetNavigatorView (&item, &view);
			if (err != NoError)
				return err;

			GS::ucsncpy (item.uName, name.ToUStr ().Get (), API_UniLongNameLen - 1);
			item.customName = true;
			if (zoom != nullptr) {
				view.zoom.xMin = zoom[0];
				view.zoom.yMin = zoom[1];
				view.zoom.xMax = zoom[2];
				view.zoom.yMax = zoom[3];
				view.saveZoom = true;
				view.ignoreSavedZoom = false;
			}
			err = ACAPI_Navigator_ChangeNavigatorView (&item, &view);

			if (view.modelViewOpt != nullptr) {
				ACAPI_FreeGDLModelViewOptionsPtr (&view.modelViewOpt->gdlOptions);
				BMKillPtr (reinterpret_cast<GSPtr*> (&view.modelViewOpt));
			}
			if (view.layerStats != nullptr)
				delete view.layerStats;
			BMKillPtr (reinterpret_cast<GSPtr*> (&view.pens));
			return err;
		}

		// Zone selected on the plan: name, number, bounding box (m) and story. 0 = found, -1 = no zone selected.
		int __cdecl GetSelectedZone (void*, wchar_t* name, int nameCap, wchar_t* number, int numberCap, double* bbox, int* floorIndex)
		{
			API_SelectionInfo selInfo = {};
			GS::Array<API_Neig> neigs;
			const GSErrCode err = ACAPI_Selection_Get (&selInfo, &neigs, false);
			BMKillHandle (reinterpret_cast<GSHandle*> (&selInfo.marquee.coords));
			if (err != NoError && err != APIERR_NOSEL)
				return err;

			for (const API_Neig& neig : neigs) {
				API_Element zone = {};
				zone.header.guid = neig.guid;
				if (ACAPI_Element_Get (&zone) != NoError || !(zone.header.type == API_ZoneID))
					continue;

				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (zone.header.guid, &memo, APIMemoMask_Polygon) != NoError)
					continue;
				bool any = false;
				for (Int32 i = 1; i <= zone.zone.poly.nCoords && memo.coords != nullptr; ++i) {
					const API_Coord& p = (*memo.coords)[i];
					if (!any) {
						bbox[0] = bbox[2] = p.x;
						bbox[1] = bbox[3] = p.y;
						any = true;
					}
					bbox[0] = GS::Min (bbox[0], p.x);
					bbox[1] = GS::Min (bbox[1], p.y);
					bbox[2] = GS::Max (bbox[2], p.x);
					bbox[3] = GS::Max (bbox[3], p.y);
				}
				ACAPI_DisposeElemMemoHdls (&memo);
				if (!any)
					continue;

				CopyOut (GS::UniString (zone.zone.roomName), name, nameCap);
				CopyOut (GS::UniString (zone.zone.roomNoStr), number, numberCap);
				*floorIndex = zone.header.floorInd;
				return 0;
			}
			return -1;
		}

		// Outline of the selected zone (first zone of the selection), without the closing point. Returns the point count (0 = none).
		int __cdecl GetZoneOutline (void*, double* xy, int maxPoints)
		{
			API_SelectionInfo selInfo = {};
			GS::Array<API_Neig> neigs;
			const GSErrCode err = ACAPI_Selection_Get (&selInfo, &neigs, false);
			BMKillHandle (reinterpret_cast<GSHandle*> (&selInfo.marquee.coords));
			if (err != NoError && err != APIERR_NOSEL)
				return 0;
			for (const API_Neig& neig : neigs) {
				API_Element zone = {};
				zone.header.guid = neig.guid;
				if (ACAPI_Element_Get (&zone) != NoError || !(zone.header.type == API_ZoneID))
					continue;
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (zone.header.guid, &memo, APIMemoMask_Polygon) != NoError)
					continue;
				int n = 0;
				if (memo.coords != nullptr) {
					Int32 last = zone.zone.poly.nCoords;
					const API_Coord& first = (*memo.coords)[1];
					const API_Coord& end = (*memo.coords)[last];
					if (last > 1 && std::fabs (first.x - end.x) < 1e-9 && std::fabs (first.y - end.y) < 1e-9)
						--last;		// closing point
					for (Int32 i = 1; i <= last && n < maxPoints; ++i) {
						xy[2 * n] = (*memo.coords)[i].x;
						xy[2 * n + 1] = (*memo.coords)[i].y;
						++n;
					}
				}
				ACAPI_DisposeElemMemoHdls (&memo);
				return n;
			}
			return 0;
		}

		// Interior elevation along a polyline of `pointCount` points (pointCount - 1 segments, one view each).
		int __cdecl CreateInteriorElevation (void* ctx, const wchar_t* name, const wchar_t* id, const double* xy, int pointCount, double depth, int floorIndex)
		{
			if (pointCount < 2)
				return APIERR_BADPARS;

			// remember the existing items of that name: the new ones are the ones not listed here
			Context* c = C (ctx);
			const GS::UniString wanted = ToUni (name);
			c->ieBefore.Clear ();
			c->iePolyline.Clear ();
			for (int i = 0; i < pointCount; ++i) {
				API_Coord p = {};
				p.x = xy[2 * i];
				p.y = xy[2 * i + 1];
				c->iePolyline.Push (p);
			}
			c->ieBaseLevel = 0.0;
			{
				API_StoryInfo storyInfo = {};
				if (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) == NoError) {
					const Int32 idx = floorIndex - storyInfo.firstStory;
					if (storyInfo.data != nullptr && idx >= 0 && floorIndex <= storyInfo.lastStory)
						c->ieBaseLevel = (*storyInfo.data)[idx].level;
					BMKillHandle (reinterpret_cast<GSHandle*> (&storyInfo.data));
				}
			}
			for (const API_NavigatorItem& it : AllMapItems (API_ProjectMap))
				if (it.itemType == API_InteriorElevationNavItem && GS::UniString (it.uName) == wanted)
					c->ieBefore.Push (it.guid);

			return ACAPI_CallUndoableCommand ("Create Interior Elevation", [&] () -> GSErrCode {
				API_Element			element = {};
				API_ElementMemo		memo = {};
				API_SubElement		marker = {};
				API_AddParType**	markAddPars = nullptr;
				double				a = 0.0, b = 0.0;
				Int32				addParNum = 0;

				element.header.type = API_InteriorElevationID;
				marker.subType = static_cast<API_SubElementType> (APISubElement_MainMarker | APISubElement_NoParams);
				GSErrCode err = ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker);
				if (err != NoError)
					return err;

				err = ACAPI_LibraryPart_GetParams (marker.subElem.object.libInd, &a, &b, &addParNum, &markAddPars);
				if (err != NoError) {
					ACAPI_DisposeElemMemoHdls (&memo);
					ACAPI_DisposeElemMemoHdls (&marker.memo);
					return err;
				}

				marker.memo.params = markAddPars;
				marker.subElem.object.useObjPens = true;
				element.header.floorInd = static_cast<short> (floorIndex);
				GS::ucsncpy (element.interiorElevation.segment.cutPlName, wanted.ToUStr ().Get (), API_UniLongNameLen - 1);
				GS::ucsncpy (element.interiorElevation.segment.cutPlIdStr, ToUni (id).ToUStr ().Get (), API_UniLongNameLen - 1);
				element.interiorElevation.segment.ieCreationSegmentDepth = depth;
				// the default vertical range is limited and absolute (around the ground floor): elevations on upper stories would see nothing.
				// The views are cropped to the story by their zoom box instead.
				element.interiorElevation.segment.vertRange = APIVerRange_Infinite;
				element.interiorElevation.segment.nMainCoord = static_cast<UInt32> (pointCount);

				memo.sectionSegmentMainCoords = reinterpret_cast<API_Coord*> (BMAllocatePtr (pointCount * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
				memo.intElevSegments = reinterpret_cast<API_SectionSegment*> (BMAllocatePtr ((pointCount - 1) * sizeof (API_SectionSegment), ALLOCATE_CLEAR, 0));
				for (int i = 0; i < pointCount; ++i) {
					memo.sectionSegmentMainCoords[i].x = xy[2 * i];
					memo.sectionSegmentMainCoords[i].y = xy[2 * i + 1];
				}

				marker.subType = APISubElement_MainMarker;
				err = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);

				ACAPI_DisposeElemMemoHdls (&memo);
				ACAPI_DisposeElemMemoHdls (&marker.memo);
				return err;
			});
		}

		int __cdecl CreateViewMapFolder (void* ctx, const wchar_t* name, int* folderHandle)
		{
			API_NavigatorSet set = {};
			set.mapId = API_PublicViewMap;
			GSErrCode err = ACAPI_Navigator_GetNavigatorSet (&set);
			if (err != NoError)
				return err;

			// the folder goes under the project's root folder (first child of the View Map root)
			API_NavigatorItem root = {};
			root.guid = set.rootGuid;
			root.mapId = API_PublicViewMap;
			GS::Array<API_NavigatorItem> top;
			err = ACAPI_Navigator_GetNavigatorChildrenItems (&root, &top);
			if (err != NoError || top.IsEmpty ())
				return err != NoError ? err : APIERR_GENERAL;
			const GS::Guid parent = APIGuid2GSGuid (top.GetFirst ().guid);

			API_NavigatorItem folder = {};
			folder.itemType = API_FolderNavItem;
			folder.mapId = API_PublicViewMap;
			GS::ucsncpy (folder.uName, ToUni (name).ToUStr ().Get (), API_UniLongNameLen - 1);
			folder.customName = true;
			err = ACAPI_Navigator_NewNavigatorView (&folder, nullptr, &parent);
			if (err != NoError)
				return err;

			Context* c = C (ctx);
			c->viewFolders.Push (folder.guid);
			*folderHandle = static_cast<int> (c->viewFolders.GetSize ()) - 1;
			return 0;
		}

		// Clones the plan of story `floorIndex` into the folder, renames it and saves the zoom box (xMin, yMin, xMax, yMax; may be null).
		int __cdecl AddPlanView (void* ctx, int folderHandle, int floorIndex, const wchar_t* viewName, const double* zoom)
		{
			Context* c = C (ctx);
			if (folderHandle < 0 || folderHandle >= static_cast<int> (c->viewFolders.GetSize ()))
				return APIERR_BADPARS;

			API_Guid source = APINULLGuid;
			for (const API_NavigatorItem& it : AllMapItems (API_ProjectMap))
				if (it.itemType == API_StoryNavItem && it.floorNum == floorIndex) {
					source = it.guid;
					break;
				}
			if (source == APINULLGuid)
				return APIERR_BADINDEX;

			API_Guid created = APINULLGuid;
			GSErrCode err = ACAPI_Navigator_CloneProjectMapItemToViewMap (&source, &c->viewFolders[folderHandle], &created);
			if (err != NoError)
				return err;
			c->planViews.Push (created);
			return SetViewNameAndZoom (created, ToUni (viewName), zoom);
		}

		// Clones the interior elevation views created by the last CreateInteriorElevation into the folder, named <prefix>_01, _02...
		// (one per segment, in segment order). viewParams = { margin, height } in m: each view is cropped to its wall.
		// The horizontal axis of an elevation view is measured from the model origin and runs against the direction of the
		// polyline segment (the camera sits on the segment and looks to its right-hand side), so for the segment P -> Q with
		// unit direction t the wall spans x = -dot (P, t) .. -dot (Q, t); the vertical axis is the absolute elevation.
		int __cdecl AddInteriorElevationViews (void* ctx, int folderHandle, const wchar_t* elevationName, const wchar_t* viewNamePrefix, const double* viewParams, int* count)
		{
			Context* c = C (ctx);
			*count = 0;
			if (folderHandle < 0 || folderHandle >= static_cast<int> (c->viewFolders.GetSize ()))
				return APIERR_BADPARS;

			struct Found { API_Guid guid; Int32 segment; };
			std::vector<Found> found;
			const GS::UniString wanted = ToUni (elevationName);
			for (const API_NavigatorItem& it : AllMapItems (API_ProjectMap))
				if (it.itemType == API_InteriorElevationNavItem && GS::UniString (it.uName) == wanted && !Contains (c->ieBefore, it.guid))
					{
					// the navigator item only carries part of the database info: ask for all of it (segment index of the elevation)
					API_DatabaseInfo info = {};
					info.databaseUnId = it.db.databaseUnId;
					API_DatabaseUnId dbId = it.db.databaseUnId;
					Int32 segment = it.db.linkedSegmentMemoIndex;
					const GSErrCode infoErr = ACAPI_Window_GetDatabaseInfo (&info, &dbId);
					if (infoErr == NoError)
						segment = info.linkedSegmentMemoIndex;
					c->diagnostics += GS::UniString::Printf ("elevation view item: GetDatabaseInfo error %d, segment index %d (navigator item: %d).\n",
															  infoErr, segment, it.db.linkedSegmentMemoIndex);
					found.push_back ({ it.guid, segment });
				}
			if (found.empty ())
				return APIERR_BADINDEX;
			std::sort (found.begin (), found.end (), [] (const Found& a, const Found& b) { return a.segment < b.segment; });

			const GS::UniString prefix = ToUni (viewNamePrefix);
			for (size_t k = 0; k < found.size (); ++k) {
				double zoom[4];
				const double* zoomPtr = nullptr;
				if (viewParams != nullptr && k + 1 < static_cast<size_t> (c->iePolyline.GetSize ())) {
					const API_Coord& p = c->iePolyline[static_cast<UIndex> (k)];
					const API_Coord& q = c->iePolyline[static_cast<UIndex> (k + 1)];
					const double len = std::sqrt ((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y));
					if (len > 1e-9) {
						const double tx = (q.x - p.x) / len, ty = (q.y - p.y) / len;
						const double a = -(p.x * tx + p.y * ty), b = -(q.x * tx + q.y * ty);
						zoom[0] = GS::Min (a, b) - viewParams[0];
						zoom[1] = c->ieBaseLevel - viewParams[0];
						zoom[2] = GS::Max (a, b) + viewParams[0];
						zoom[3] = c->ieBaseLevel + viewParams[1] + viewParams[0];
						zoomPtr = zoom;
					}
				}

				API_Guid source = found[k].guid;
				API_Guid created = APINULLGuid;
				GSErrCode err = ACAPI_Navigator_CloneProjectMapItemToViewMap (&source, &c->viewFolders[folderHandle], &created);
				if (err != NoError)
					return err;
				++*count;
				c->elevationViews.Push (created);
				err = SetViewNameAndZoom (created, prefix + GS::UniString::Printf ("_%02d", *count), zoomPtr);
				if (err != NoError)
					return err;
			}
			return 0;
		}
		int __cdecl CreateLayoutSubset (void* ctx, const wchar_t* name, int* subsetIndex)
		{
			API_SubSet subSet = {};
			GSErrCode err = ACAPI_Navigator_GetSubSetDefault (&subSet);
			if (err != NoError)
				return err;
			const GS::UniString subsetName = ToUni (name);
			GS::ucsncpy (subSet.name, subsetName.ToUStr ().Get (), API_LongNameLen - 1);
			err = ACAPI_Navigator_CreateSubSet (&subSet);
			if (err != NoError)
				return err;

			// the snapshot must include the new subset: CreateLayout takes indexes into it
			Context* c = C (ctx);
			RefreshLayoutBook (*c);
			*subsetIndex = -1;
			for (UIndex i = 0; i < c->subsets.GetSize (); ++i)
				if (GS::UniString (c->subsets[i].uName) == subsetName)
					*subsetIndex = static_cast<int> (i);
			return *subsetIndex >= 0 ? 0 : APIERR_GENERAL;
		}

		// Places a saved view on a layout as a drawing linked to the view (ACAPI drawing element, drawingGuid = view).
		// viewKind: 0 = plan view, 1 = elevation view; viewIndex = creation order. x, y: lower-left corner on the layout (m).
		// targetScale: the drawing is shown at 1:targetScale. width / height: size of the placed drawing (m).
		int __cdecl PlaceViewOnLayout (void* ctx, const wchar_t* layoutName, int viewKind, int viewIndex, double x, double y,
									   double targetScale, double* width, double* height)
		{
			Context* c = C (ctx);
			const GS::Array<API_Guid>& views = viewKind == 0 ? c->planViews : c->elevationViews;
			if (viewIndex < 0 || viewIndex >= static_cast<int> (views.GetSize ()) || targetScale <= 0.0)
				return APIERR_BADPARS;
			const API_Guid viewGuid = views[viewIndex];

			// the layout was created after the snapshot: look it up again (the last one of that name)
			RefreshLayoutBook (*c);
			const GS::UniString wanted = ToUni (layoutName);
			int layoutIdx = -1;
			for (UIndex i = 0; i < c->layouts.GetSize (); ++i)
				if (c->layouts[i].name == wanted)
					layoutIdx = static_cast<int> (i);
			if (layoutIdx < 0)
				return APIERR_BADINDEX;

			// drawing scale of the source view: ratio = view scale / target scale
			double viewScale = 100.0;
			{
				API_NavigatorItem item = {};
				item.guid = viewGuid;
				item.mapId = API_PublicViewMap;
				if (ACAPI_Navigator_GetNavigatorItem (&viewGuid, &item) == NoError) {
					API_NavigatorView view = {};
					if (ACAPI_Navigator_GetNavigatorView (&item, &view) == NoError) {
						if (view.drawingScale > 0)
							viewScale = view.drawingScale;
						if (view.modelViewOpt != nullptr) {
							ACAPI_FreeGDLModelViewOptionsPtr (&view.modelViewOpt->gdlOptions);
							BMKillPtr (reinterpret_cast<GSPtr*> (&view.modelViewOpt));
						}
						if (view.layerStats != nullptr)
							delete view.layerStats;
						BMKillPtr (reinterpret_cast<GSPtr*> (&view.pens));
					}
				}
			}

			API_DatabaseInfo original = {};
			GSErrCode err = ACAPI_Database_GetCurrentDatabase (&original);
			if (err != NoError)
				return err;
			API_DatabaseInfo target = {};
			target.typeID = APIWind_LayoutID;
			target.databaseUnId = c->layouts[layoutIdx].dbId;
			err = ACAPI_Database_ChangeCurrentDatabase (&target);
			if (err != NoError)
				return err;

			API_Guid created = APINULLGuid;
			err = ACAPI_CallUndoableCommand ("Place view on layout", [&] () -> GSErrCode {
				API_Element element = {};
				element.header.type = API_DrawingID;
				GSErrCode e = ACAPI_Element_GetDefaults (&element, nullptr);
				if (e != NoError)
					return e;
				element.drawing.drawingGuid = viewGuid;
				element.drawing.ratio = viewScale / targetScale;
				element.drawing.anchorPoint = APIAnc_LB;
				element.drawing.pos.x = x;
				element.drawing.pos.y = y;
				API_ElementMemo memo = {};
				e = ACAPI_Element_Create (&element, &memo);
				ACAPI_DisposeElemMemoHdls (&memo);
				if (e == NoError)
					created = element.header.guid;
				return e;
			});

			if (err == NoError && created != APINULLGuid) {
				API_Element placed = {};
				placed.header.guid = created;
				if (ACAPI_Element_Get (&placed) == NoError) {
					*width = placed.drawing.bounds.xMax - placed.drawing.bounds.xMin;
					*height = placed.drawing.bounds.yMax - placed.drawing.bounds.yMin;
				}
			}
			if (err == NoError && created != APINULLGuid) {
				c->placedDrawings.Push (created);
				c->placedLayoutDb = target.databaseUnId;
			}
			ACAPI_Database_ChangeCurrentDatabase (&original);
			return err;
		}

		// Refreshes the content of the drawings placed by PlaceViewOnLayout; their bounds before / after go to the diagnostics.
		int __cdecl UpdatePlacedDrawings (void* ctx)
		{
			Context* c = C (ctx);
			if (c->placedDrawings.IsEmpty ())
				return 0;

			API_DatabaseInfo original = {};
			GSErrCode err = ACAPI_Database_GetCurrentDatabase (&original);
			if (err != NoError)
				return err;
			API_DatabaseInfo target = {};
			target.typeID = APIWind_LayoutID;
			target.databaseUnId = c->placedLayoutDb;
			err = ACAPI_Database_ChangeCurrentDatabase (&target);
			if (err != NoError)
				return err;

			auto logBounds = [&] (const char* when) {
				for (const API_Guid& g : c->placedDrawings) {
					API_Element el = {};
					el.header.guid = g;
					if (ACAPI_Element_Get (&el) == NoError)
						c->diagnostics += GS::UniString::Printf ("drawing %s: pos %.3f,%.3f bounds %.3f,%.3f - %.3f,%.3f\n", when,
							el.drawing.pos.x, el.drawing.pos.y, el.drawing.bounds.xMin, el.drawing.bounds.yMin, el.drawing.bounds.xMax, el.drawing.bounds.yMax);
				}
			};
			logBounds ("before update");
			err = ACAPI_Drawing_Update_Drawings (c->placedDrawings);
			logBounds ("after update");
			c->diagnostics += GS::UniString::Printf ("Update_Drawings: error %d.\n", err);

			ACAPI_Database_ChangeCurrentDatabase (&original);
			return err;
		}

		// ---- Align Views ---------------------------------------------------------------------------

		// Makes a layout the current database for the lifetime of the object
		struct LayoutScope {
			API_DatabaseInfo	original = {};
			bool				switched = false;
			explicit LayoutScope (const API_DatabaseUnId& layoutId)
			{
				if (ACAPI_Database_GetCurrentDatabase (&original) != NoError)
					return;
				API_DatabaseInfo target = {};
				target.typeID = APIWind_LayoutID;
				target.databaseUnId = layoutId;
				switched = ACAPI_Database_ChangeCurrentDatabase (&target) == NoError;
			}
			~LayoutScope ()
			{
				if (switched)
					ACAPI_Database_ChangeCurrentDatabase (&original);
			}
		};

		bool LoadDrawings (Context& c, int layoutIndex)
		{
			if (layoutIndex < 0 || layoutIndex >= static_cast<int> (c.layouts.GetSize ()))
				return false;
			LayoutScope scope (c.layouts[layoutIndex].dbId);
			if (!scope.switched)
				return false;
			c.drawingCache.Clear ();
			if (ACAPI_Element_GetElemList (API_DrawingID, &c.drawingCache) != NoError)
				return false;
			c.drawingCacheLayout = layoutIndex;
			return true;
		}

		// Number of drawings on the layout (index into the GetLayoutInfo list), -1 on error.
		int __cdecl GetDrawingCount (void* ctx, int layoutIndex)
		{
			Context* c = C (ctx);
			return LoadDrawings (*c, layoutIndex) ? static_cast<int> (c->drawingCache.GetSize ()) : -1;
		}

		// name: the source view's name; key: the source view's GUID (same view placed on two layouts = same key);
		// v = pos.x, pos.y (m), ratio, angle (rad), anchor point id, modelOffset.x, modelOffset.y, 1 = the view is a floor plan
		int __cdecl GetDrawing (void* ctx, int layoutIndex, int i, wchar_t* name, int nameCap, wchar_t* key, int keyCap, double* v)
		{
			Context* c = C (ctx);
			if (c->drawingCacheLayout != layoutIndex && !LoadDrawings (*c, layoutIndex))
				return APIERR_BADPARS;
			if (i < 0 || i >= static_cast<int> (c->drawingCache.GetSize ()))
				return APIERR_BADINDEX;
			LayoutScope scope (c->layouts[layoutIndex].dbId);
			if (!scope.switched)
				return APIERR_BADDATABASE;

			API_Element el = {};
			el.header.guid = c->drawingCache[i];
			const GSErrCode err = ACAPI_Element_Get (&el);
			if (err != NoError)
				return err;

			GS::UniString viewName (el.drawing.name, CC_UTF8);
			GS::UniString viewKey = APIGuidToString (el.drawing.drawingGuid);
			bool isPlan = false;
			API_DrawingLinkInfo link = {};
			const GSErrCode linkErr = ACAPI_Drawing_GetDrawingLink (&c->drawingCache[i], &link);
			if (linkErr == NoError) {
				// a drawing linked to a saved view: the view type tells floor plans from elevations, sections, details...
				isPlan = link.linkTypeID == API_DrawingLink_InternalViewID && link.viewType == API_ViewNodeStory;
				if (link.name[0] != 0)
					viewName = GS::UniString (link.name, CC_UTF8);
				if (link.linkGuid != APINULLGuid)
					viewKey = APIGuidToString (link.linkGuid);
				if (link.linkPath != nullptr)
					delete link.linkPath;
			}
			c->diagnostics += GS::UniString::Printf ("drawing %d of layout %d: GetDrawingLink error %d, link type %d, view type %d, view '%T'.\n",
													 i, layoutIndex, linkErr, static_cast<int> (link.linkTypeID), static_cast<int> (link.viewType), viewName.ToPrintf ());
			CopyOut (viewName, name, nameCap);
			CopyOut (viewKey, key, keyCap);
			v[0] = el.drawing.pos.x;
			v[1] = el.drawing.pos.y;
			v[2] = el.drawing.ratio;
			v[3] = el.drawing.angle;
			v[4] = static_cast<double> (el.drawing.anchorPoint);
			v[5] = el.drawing.modelOffset.x;
			v[6] = el.drawing.modelOffset.y;
			v[7] = isPlan ? 1.0 : 0.0;
			return 0;
		}

		// flags: 1 = position (+ anchor + model offset), 2 = scale ratio, 4 = rotation. Same layout of `v` as GetDrawing.
		int __cdecl SetDrawing (void* ctx, int layoutIndex, int i, const double* v, unsigned int flags)
		{
			Context* c = C (ctx);
			if (c->drawingCacheLayout != layoutIndex && !LoadDrawings (*c, layoutIndex))
				return APIERR_BADPARS;
			if (i < 0 || i >= static_cast<int> (c->drawingCache.GetSize ()))
				return APIERR_BADINDEX;
			const API_Guid guid = c->drawingCache[i];

			LayoutScope scope (c->layouts[layoutIndex].dbId);
			if (!scope.switched)
				return APIERR_BADDATABASE;
			return ACAPI_CallUndoableCommand ("Align views", [&] () -> GSErrCode {
				API_Element element = {};
				API_Element mask = {};
				element.header.guid = guid;
				GSErrCode e = ACAPI_Element_Get (&element);
				if (e != NoError)
					return e;

				// 1. properties that are plain fields of the drawing
				ACAPI_ELEMENT_MASK_CLEAR (mask);
				bool changeFields = false;
				if (flags & 1) {
					const API_AnchorID anchor = static_cast<API_AnchorID> (static_cast<int> (v[4]));
					if (element.drawing.anchorPoint != anchor) {
						element.drawing.anchorPoint = anchor;
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, anchorPoint);
						changeFields = true;
					}
					if (element.drawing.modelOffset.x != v[5] || element.drawing.modelOffset.y != v[6]) {
						element.drawing.modelOffset.x = v[5];
						element.drawing.modelOffset.y = v[6];
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, modelOffset);
						changeFields = true;
					}
				}
				if (flags & 2) {
					element.drawing.ratio = v[2];
					ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, ratio);
					changeFields = true;
				}
				if (flags & 4) {
					element.drawing.angle = v[3];
					ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, angle);
					changeFields = true;
				}
				if (changeFields) {
					e = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
					if (e != NoError)
						return e;
				}

				// 2. the position is a real move (drag): the drawing keeps its frame (clip polygon) together with its origin
				if (flags & 1) {
					API_Element current = {};
					current.header.guid = guid;
					e = ACAPI_Element_Get (&current);
					if (e != NoError)
						return e;
					const double dx = v[0] - current.drawing.pos.x;
					const double dy = v[1] - current.drawing.pos.y;
					if (std::fabs (dx) > 1e-9 || std::fabs (dy) > 1e-9) {
						API_Neig neig = {};
						neig.guid = guid;
						neig.neigID = APINeig_DrawingCenter;
						neig.inIndex = 1;
						GS::Array<API_Neig> items;
						items.Push (neig);
						API_EditPars editPars = {};
						editPars.typeID = APIEdit_Drag;
						editPars.withDelete = true;
						editPars.endC.x = dx;
						editPars.endC.y = dy;
						e = ACAPI_Element_Edit (&items, editPars);
					}
				}
				return e;
			});
		}

		// ---- Copy Views ----

		// Looks the GUID text up among the selection snapshot and the drawings of the cached layout
		bool ResolveGuid (const Context& c, const GS::UniString& text, API_Guid& guid)
		{
			for (const API_Guid& g : c.selection)
				if (APIGuidToString (g) == text) {
					guid = g;
					return true;
				}
			for (const API_Guid& g : c.drawingCache)
				if (APIGuidToString (g) == text) {
					guid = g;
					return true;
				}
			return false;
		}

		// Selection of the layout that is open. layoutIndex = position in the layout list; returns the number of selected elements, -1 = no layout open.
		int __cdecl GetSelectionCount (void* ctx, int* layoutIndex)
		{
			Context* c = C (ctx);
			c->selection.Clear ();
			c->selectionTypes.Clear ();
			*layoutIndex = -1;

			API_DatabaseInfo current = {};
			if (ACAPI_Database_GetCurrentDatabase (&current) == NoError && current.typeID == APIWind_LayoutID) {
				RefreshLayoutBook (*c);
				for (UIndex i = 0; i < c->layouts.GetSize (); ++i)
					if (c->layouts[i].dbId == current.databaseUnId)
						*layoutIndex = static_cast<int> (i);
			}

			API_SelectionInfo selInfo = {};
			GS::Array<API_Neig> selNeigs;
			const GSErrCode err = ACAPI_Selection_Get (&selInfo, &selNeigs, false);
			BMKillHandle (reinterpret_cast<GSHandle*> (&selInfo.marquee.coords));
			if (err != NoError && err != APIERR_NOSEL)
				return -1;

			for (const API_Neig& neig : selNeigs) {
				API_Elem_Head head = {};
				head.guid = neig.guid;
				if (ACAPI_Element_GetHeader (&head) != NoError)
					continue;
				c->selection.Push (neig.guid);
				c->selectionTypes.Push (static_cast<int> (head.type.typeID));
			}
			return static_cast<int> (c->selection.GetSize ());
		}

		int __cdecl GetSelectionItem (void* ctx, int index, wchar_t* guid, int guidCap, int* typeId)
		{
			Context* c = C (ctx);
			if (index < 0 || index >= static_cast<int> (c->selection.GetSize ()))
				return APIERR_BADINDEX;
			CopyOut (APIGuidToString (c->selection[index]), guid, guidCap);
			*typeId = c->selectionTypes[index];
			return 0;
		}

		int __cdecl FindDrawingIndex (void* ctx, int layoutIndex, const wchar_t* guid)
		{
			Context* c = C (ctx);
			if (!LoadDrawings (*c, layoutIndex))
				return -1;
			const GS::UniString wanted = ToUni (guid);
			for (UIndex i = 0; i < c->drawingCache.GetSize (); ++i)
				if (APIGuidToString (c->drawingCache[i]) == wanted)
					return static_cast<int> (i);
			return -1;
		}

		// Creates a copy of element `guid` (found on srcLayout) on dstLayout: the whole element and its memo, so a drawing keeps its view link,
		// scale, rotation, anchor and frame, and everything stays at the same position on the sheet.
		int __cdecl CopyElementToLayout (void* ctx, int srcLayoutIndex, const wchar_t* guid, int dstLayoutIndex)
		{
			Context* c = C (ctx);
			const int layoutCount = static_cast<int> (c->layouts.GetSize ());
			if (srcLayoutIndex < 0 || srcLayoutIndex >= layoutCount || dstLayoutIndex < 0 || dstLayoutIndex >= layoutCount || dstLayoutIndex == srcLayoutIndex)
				return APIERR_BADPARS;
			if (c->drawingCacheLayout != srcLayoutIndex)
				LoadDrawings (*c, srcLayoutIndex);
			API_Guid srcGuid = APINULLGuid;
			if (!ResolveGuid (*c, ToUni (guid), srcGuid))
				return APIERR_BADID;

			API_Element source = {};
			API_ElementMemo memo = {};
			API_Guid linkedView = APINULLGuid;		// navigator item a drawing shows (its content is not in the element itself)
			{
				LayoutScope scope (c->layouts[srcLayoutIndex].dbId);
				if (!scope.switched)
					return APIERR_BADDATABASE;
				source.header.guid = srcGuid;
				GSErrCode err = ACAPI_Element_Get (&source);
				if (err != NoError)
					return err;
				if (source.header.type.typeID == API_DrawingID) {
					API_DrawingLinkInfo link = {};
					const GSErrCode linkErr = ACAPI_Drawing_GetDrawingLink (&srcGuid, &link);
					c->diagnostics += GS::UniString::Printf ("CopyElementToLayout: source drawing link error %d, link type %d, link guid %s, drawingGuid %s.\n",
						linkErr, static_cast<int> (link.linkTypeID), APIGuidToString (link.linkGuid).ToCStr ().Get (), APIGuidToString (source.drawing.drawingGuid).ToCStr ().Get ());
					if (linkErr == NoError) {
						if (link.linkGuid != APINULLGuid)
							linkedView = link.linkGuid;
						if (link.linkPath != nullptr)
							delete link.linkPath;
					}
				}
				err = ACAPI_Element_GetMemo (srcGuid, &memo, source.header.type.typeID == API_DrawingID ? APIMemoMask_Polygon : APIMemoMask_All);
				if (err != NoError) {
					ACAPI_DisposeElemMemoHdls (&memo);
					return err;
				}
			}

			GSErrCode result = NoError;
			{
				LayoutScope scope (c->layouts[dstLayoutIndex].dbId);
				if (!scope.switched) {
					ACAPI_DisposeElemMemoHdls (&memo);
					return APIERR_BADDATABASE;
				}
				API_Guid created = APINULLGuid;
				result = ACAPI_CallUndoableCommand ("Copy to layout", [&] () -> GSErrCode {
					API_Element element = {};
					element.header.type = source.header.type;
					GSErrCode e = ACAPI_Element_GetDefaults (&element, nullptr);
					if (e != NoError)
						return e;
					if (source.header.type.typeID == API_DrawingID) {
						element.drawing = source.drawing;
						element.header.guid = APINULLGuid;
						if (linkedView != APINULLGuid)
							element.drawing.drawingGuid = linkedView;
					} else {
						element = source;
						element.header.guid = APINULLGuid;
					}
					e = ACAPI_Element_Create (&element, &memo);
					if (e == NoError)
						created = element.header.guid;
					return e;
				});
				if (result == NoError && created != APINULLGuid && source.header.type.typeID == API_DrawingID) {
					GS::Array<API_Guid> toUpdate;
					toUpdate.Push (created);
					const GSErrCode upd = ACAPI_Drawing_Update_Drawings (toUpdate);
					if (upd != NoError)
						c->diagnostics += GS::UniString::Printf ("CopyElementToLayout: Update_Drawings error %d.\n", upd);
				}
			}
			ACAPI_DisposeElemMemoHdls (&memo);
			return result;
		}
		// ---- Element access in any window ----

		// First parameter named `name` of a library-part element's memo, nullptr when there is none
		API_AddParType* FindStringParam (API_ElementMemo& memo, const char* name)
		{
			if (memo.params == nullptr)
				return nullptr;
			const Int32 count = static_cast<Int32> (BMGetHandleSize (reinterpret_cast<GSHandle> (memo.params)) / sizeof (API_AddParType));
			for (Int32 i = 0; i < count; ++i) {
				API_AddParType& p = (*memo.params)[i];
				if (p.typeID == APIParT_CString && std::strcmp (p.name, name) == 0)
					return &p;
			}
			return nullptr;
		}

		int __cdecl GetElementPlacement (void* ctx, const wchar_t* guid, double* v)
		{
			Context* c = C (ctx);
			API_Guid g = APINULLGuid;
			if (!ResolveGuid (*c, ToUni (guid), g))
				return APIERR_BADID;
			API_Element el = {};
			el.header.guid = g;
			const GSErrCode err = ACAPI_Element_Get (&el);
			if (err != NoError)
				return err;
			if (el.header.type.typeID != API_ObjectID && el.header.type.typeID != API_LampID)
				return APIERR_BADPARS;
			v[0] = el.object.pos.x;
			v[1] = el.object.pos.y;
			v[2] = el.object.angle;
			return 0;
		}

		int __cdecl GetElementTextParam (void* ctx, const wchar_t* guid, const wchar_t* name, wchar_t* value, int cap)
		{
			Context* c = C (ctx);
			API_Guid g = APINULLGuid;
			if (!ResolveGuid (*c, ToUni (guid), g))
				return APIERR_BADID;
			API_ElementMemo memo = {};
			GSErrCode err = ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars);
			if (err != NoError) {
				ACAPI_DisposeElemMemoHdls (&memo);
				return err;
			}
			const API_AddParType* p = FindStringParam (memo, ToUni (name).ToCStr (CC_UTF8).Get ());
			if (p == nullptr) {
				ACAPI_DisposeElemMemoHdls (&memo);
				return APIERR_BADPARS;
			}
			CopyOut (GS::UniString (p->value.uStr), value, cap);
			ACAPI_DisposeElemMemoHdls (&memo);
			return 0;
		}

		int __cdecl SetElementTextParams (void* ctx, int count, const wchar_t* const* guids, const wchar_t* const* names, const wchar_t* const* values)
		{
			Context* c = C (ctx);
			return ACAPI_CallUndoableCommand ("Change text parameters", [&] () -> GSErrCode {
				for (int i = 0; i < count; ++i) {
					API_Guid g = APINULLGuid;
					if (!ResolveGuid (*c, ToUni (guids[i]), g))
						return APIERR_BADID;
					API_Element element = {};
					element.header.guid = g;
					GSErrCode e = ACAPI_Element_Get (&element);
					if (e != NoError)
						return e;
					API_ElementMemo memo = {};
					e = ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars);
					if (e == NoError) {
						API_AddParType* p = FindStringParam (memo, ToUni (names[i]).ToCStr (CC_UTF8).Get ());
						if (p == nullptr) {
							e = APIERR_BADPARS;
						} else {
							memset (p->value.uStr, 0, sizeof (p->value.uStr));
							GS::ucsncpy (p->value.uStr, ToUni (values[i]).ToUStr ().Get (), API_UAddParStrLen - 1);
							API_Element mask = {};
							ACAPI_ELEMENT_MASK_CLEAR (mask);
							e = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_AddPars, true);
						}
					}
					ACAPI_DisposeElemMemoHdls (&memo);
					if (e != NoError)
						return e;
				}
				return NoError;
			});
		}
		int __cdecl RenameLayout (void* ctx, int i, const wchar_t* newName, const wchar_t* newId)
		{
			const Context* c = C (ctx);
			if (i < 0 || i >= static_cast<int> (c->layouts.GetSize ()))
				return APIERR_BADPARS;

			// ACAPI_Database_ModifyDatabase renames a layout and changes its reference ID; like CreateLayout it is
			// a non-undoable navigator change, so it must not run inside ACAPI_CallUndoableCommand.
			API_DatabaseInfo info = {};
			info.databaseUnId = c->layouts[i].dbId;
			API_DatabaseUnId idCopy = info.databaseUnId;
			const GSErrCode err = ACAPI_Window_GetDatabaseInfo (&info, &idCopy);
			if (err != NoError)
				return err;

			const Context::Layout& l = c->layouts[i];

			// Name: database info (info.ref is left as it is: it is the composed ID, not the one shown in the Navigator)
			if (ToUni (newName) != l.name) {
				memset (info.name, 0, sizeof (info.name));
				GS::ucsncpy (info.name, ToUni (newName).ToUStr ().Get (), API_UniLongNameLen - 1);
				const GSErrCode nameErr = ACAPI_Database_ModifyDatabase (&info);
				if (nameErr != NoError)
					return nameErr;
			}

			// ID: the layout's Custom ID (Layout Settings > ID assignment), as when a layout is created
			if (ToUni (newId) != l.id) {
				API_DatabaseUnId dbId = l.dbId;
				API_LayoutInfo layout = {};
				GSErrCode idErr = ACAPI_Navigator_GetLayoutSets (&layout, &dbId);
				if (idErr != NoError)
					return idErr;
				layout.customLayoutNumbering = true;
				memset (layout.customLayoutNumber, 0, sizeof (layout.customLayoutNumber));
				CHTruncate (ToUni (newId).ToCStr (CC_UTF8).Get (), layout.customLayoutNumber, sizeof (layout.customLayoutNumber));
				idErr = ACAPI_Navigator_ChangeLayoutSets (&layout, &dbId);
				delete layout.customData;	// allocated by GetLayoutSets
				if (idErr != NoError)
					return idErr;
			}
			return NoError;
	}

} // namespace


Context* CreateContext ()
{
	Context* c = new Context ();
	CollectMasters (*c);
	CollectLayoutBook (*c);
	return c;
}

void DestroyContext (Context* ctx)
{
	delete ctx;
}

SheetXL_Host MakeHost (Context* ctx)
{
	SheetXL_Host h = {};
	h.size = sizeof (SheetXL_Host);
	h.ctx = ctx;
	h.ShowMessage = &ShowMessage;
	h.GetDiagnostics = &GetDiagnostics;
	h.GetMasterCount = &GetMasterCount;
	h.GetMasterName = &GetMasterName;
	h.GetSubsetCount = &GetSubsetCount;
	h.GetSubsetName = &GetSubsetName;
	h.GetLayoutIdCount = &GetLayoutIdCount;
	h.GetLayoutId = &GetLayoutId;
	h.CreateLayout = &CreateLayout;
		h.GetLayoutCount = &GetLayoutCount;
		h.GetLayoutInfo = &GetLayoutInfo;
		h.RenameLayout = &RenameLayout;
		h.GetSelectedZone = &GetSelectedZone;
		h.CreateInteriorElevation = &CreateInteriorElevation;
		h.CreateViewMapFolder = &CreateViewMapFolder;
		h.AddPlanView = &AddPlanView;
		h.AddInteriorElevationViews = &AddInteriorElevationViews;
		h.CreateLayoutSubset = &CreateLayoutSubset;
		h.PlaceViewOnLayout = &PlaceViewOnLayout;
		h.UpdatePlacedDrawings = &UpdatePlacedDrawings;
		h.GetZoneOutline = &GetZoneOutline;
		h.GetDrawingCount = &GetDrawingCount;
		h.GetDrawing = &GetDrawing;
		h.SetDrawing = &SetDrawing;
		h.GetSelectionCount = &GetSelectionCount;
		h.GetSelectionItem = &GetSelectionItem;
		h.FindDrawingIndex = &FindDrawingIndex;
		h.CopyElementToLayout = &CopyElementToLayout;
		h.GetElementPlacement = &GetElementPlacement;
		h.GetElementTextParam = &GetElementTextParam;
		h.SetElementTextParams = &SetElementTextParams;
	return h;
}

} // namespace ArchicadHostHelper
} // namespace AddonManager
