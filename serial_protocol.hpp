#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

[[nodiscard]] std::vector<std::uint16_t> parse_scan_response(
	std::span<const std::uint8_t> response,
	std::size_t expected_points
);
