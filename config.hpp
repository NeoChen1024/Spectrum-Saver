#pragma once

#include <cstddef>
#include <string_view>

// tinySA zero level: 128 for tinySA, 174 for tinySA Ultra.
inline constexpr int ZERO_LEVEL = 128;
inline constexpr int ZERO_LEVEL_ULTRA = 174;

inline constexpr std::string_view FONT_FAMILY{"Iosevka Term"};

inline constexpr int BANNER_HEIGHT = 64;
inline constexpr int FOOTER_HEIGHT = 24;
inline constexpr std::string_view BANNER_COLOR{"white"};
inline constexpr std::string_view FOOTER_COLOR{"yellow"};

[[nodiscard]] constexpr double pixels_to_points(const double pixels) noexcept
{
	return pixels * 72.0 / 96.0;
}

inline constexpr std::size_t MIN_GRIDLINES = 6;
