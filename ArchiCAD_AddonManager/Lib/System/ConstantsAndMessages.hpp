#ifndef MANAGER_CONSTANTSANDMESSAGES_HPP
#define MANAGER_CONSTANTSANDMESSAGES_HPP

namespace AddonManager {
namespace ConstantsAndMessages {

	// Resource IDs ('STR#' ids in Resources\RINT\*.grc)
	inline constexpr short ADDON_NAME_STRINGS = 32000;

	// One menu resource per button: with 3+ rows Archicad reads row 2 as a submenu name.
	// Same title in both = same tab, two buttons, no submenu.
	inline constexpr short MENU_STRINGS_MANUAL   = 32500;
	inline constexpr short MENU_STRINGS_FACELESS = 32501;

	// Command ids handled by Panel_Manager
	inline constexpr short MENU_MANAGER_MANUAL   = 1;
	inline constexpr short MENU_MANAGER_FACELESS = 2;

} // namespace ConstantsAndMessages
} // namespace AddonManager

#endif
