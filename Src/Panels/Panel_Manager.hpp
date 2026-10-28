#ifndef PANEL_MANAGER_HPP
#define PANEL_MANAGER_HPP

namespace AddonManager {
namespace Panels {

	// Menu items of the "Add-in Manager" panel. Returns true when `itemIndex` belongs to this panel and was run.
	bool Panel_Manager_HandleMenuItem (short itemIndex);

} // namespace Panels
} // namespace AddonManager

#endif
