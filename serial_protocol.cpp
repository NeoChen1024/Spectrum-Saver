#include "serial_protocol.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

std::vector<std::uint16_t> parse_scan_response(
	const std::span<const std::uint8_t> response,
	const std::size_t expected_points
)
{
	const auto marker = std::ranges::find(response, static_cast<std::uint8_t>('{'));
	if(marker == response.end())
		throw std::runtime_error("Scan response does not contain a data marker");

	std::vector<std::uint16_t> samples;
	samples.reserve(expected_points);
	std::size_t offset = static_cast<std::size_t>(marker - response.begin()) + 1;
	while(offset < response.size() && response[offset] == static_cast<std::uint8_t>('x'))
	{
		if(response.size() - offset < 3)
			throw std::runtime_error("Scan response ends in the middle of a sample");
		const auto low = static_cast<std::uint16_t>(response[offset + 1]);
		const auto high = static_cast<std::uint16_t>(response[offset + 2]);
		samples.emplace_back(static_cast<std::uint16_t>(low | (high << 8)));
		offset += 3;
	}

	if(samples.size() != expected_points)
	{
		throw std::runtime_error(std::format(
			"Scan response contains {} points; expected {}", samples.size(), expected_points));
	}
	return samples;
}
