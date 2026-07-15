#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <iosfwd>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct LogHeader
{
	double start_freq{};
	double stop_freq{};
	std::size_t steps{};
	float rbw{};
	std::string start_time;
	std::string end_time;
};

struct LogProblems
{
	bool variant_interval{};
	bool time_range_not_divisible_by_record_count{};
	bool interval_not_divisible_by_60{};
	bool negative_interval{};
	bool time_overlap{};
};

template<typename... Args>
void print(std::format_string<Args...> format_string, Args&&... args)
{
	std::cout << std::format(format_string, std::forward<Args>(args)...);
}

void throw_if(bool condition, std::string_view message);

[[nodiscard]] std::chrono::system_clock::time_point now();
[[nodiscard]] std::string time_str();
[[nodiscard]] std::chrono::sys_seconds time_from_str(std::string_view str);
[[nodiscard]] bool parse_header(std::string_view line, LogHeader &header);
void parse_logfile(
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers,
	std::istream &logfile_stream
);
[[nodiscard]] bool check_logfile_time_consistency(
	std::span<const LogHeader> headers,
	LogProblems &problems
);

[[nodiscard]] std::vector<std::int64_t> calculate_gridline_columns(
	std::int64_t start_frequency_hz,
	std::int64_t stop_frequency_hz,
	std::size_t steps,
	std::size_t minimum_gridlines
);
