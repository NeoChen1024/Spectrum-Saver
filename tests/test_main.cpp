#include "common.hpp"
#include "serial_protocol.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view description)
{
	if(condition)
		return;
	std::cerr << std::format("FAIL: {}\n", description);
	++failures;
}

template<typename Function>
void check_throws(Function &&function, const std::string_view description)
{
	try
	{
		std::invoke(std::forward<Function>(function));
		check(false, description);
	}
	catch(const std::exception &)
	{
	}
}

void test_header_parsing()
{
	LogHeader header;
	check(parse_header("$ 87.5,108.0,2051,100.0,20230317T113315,20230317T113317", header),
		"valid header parses");
	check(header.steps == 2051 && header.start_freq == 87.5 && header.rbw == 100.0F,
		"parsed header values are preserved");
	check(!parse_header("$ 87.5,108.0,nope,100.0,20230317T113315,20230317T113317", header),
		"invalid numeric header is rejected");
	check(!parse_header("$ 87.5,108.0,2051,100.0,20230229T000000,20230317T113317", header),
		"invalid calendar date is rejected");
	check(!parse_header("$ 87.5,108.0,2051,100.0,20230317T113315junk,20230317T113317", header),
		"timestamp trailing garbage is rejected");
}

void test_log_parsing()
{
	std::istringstream input{
		"$ 1.0,2.0,2,10.0,20230101T000000,20230101T000001\n"
		"-100.5\n"
		"-99.0\n"
		"\n"
		"$ 1.0,2.0,2,10.0,20230101T000100,20230101T000101\n"
		"-98.5\n"
		"-97.0\n"
		"\n"
		"$ 1.0,2.0,2,10.0,20230101T000200,20230101T000201\n"
		"-96.5\n"
		"-95.0\n"};
	std::vector<float> power;
	std::vector<LogHeader> headers;
	parse_logfile(power, headers, input);
	check(headers.size() == 3 && power.size() == 6, "multiple complete text records parse");
	check(power.front() == -100.5F && power.back() == -95.0F, "power values parse exactly");

	check_throws([] {
		std::istringstream bad{
			"$ 1.0,2.0,1,10.0,20230101T000000,20230101T000001\n-1.0junk\n"};
		std::vector<float> values;
		std::vector<LogHeader> parsed_headers;
		parse_logfile(values, parsed_headers, bad);
	}, "power trailing garbage is rejected");

	check_throws([] {
		std::istringstream incomplete{
			"$ 1.0,2.0,2,10.0,20230101T000000,20230101T000001\n-1.0\n"};
		std::vector<float> values;
		std::vector<LogHeader> parsed_headers;
		parse_logfile(values, parsed_headers, incomplete);
	}, "incomplete final record is rejected");
}

void test_time_consistency()
{
	LogProblems problems;
	const std::vector<LogHeader> one_record{{
		.start_freq = 1,
		.stop_freq = 2,
		.steps = 1,
		.rbw = 10,
		.start_time = "20230101T000000",
		.end_time = "20230101T000001"
	}};
	check(!check_logfile_time_consistency(one_record, problems),
		"one record does not divide by zero");

	auto zero_interval = one_record;
	zero_interval.push_back(one_record.front());
	check(check_logfile_time_consistency(zero_interval, problems) && problems.negative_interval,
		"zero interval is reported without modulo by zero");
}

void test_scan_response()
{
	const std::vector<std::uint8_t> response{
		'e', 'c', 'h', 'o', '{', 'x', 0x34, 0x80, 'x', 0x78, 0x56, '}'
	};
	const auto samples = parse_scan_response(response, 2);
	check(samples.size() == 2 && samples[0] == 0x8034 && samples[1] == 0x5678,
		"scan samples decode as unsigned little-endian values");
	check_throws([] {
		const std::vector<std::uint8_t> missing_marker{'x', 1, 2};
		(void)parse_scan_response(missing_marker, 1);
	}, "missing scan marker is rejected");
	check_throws([] {
		const std::vector<std::uint8_t> truncated{'{', 'x', 1};
		(void)parse_scan_response(truncated, 1);
	}, "truncated scan sample is rejected");
	check_throws([] {
		const std::vector<std::uint8_t> short_response{'{', 'x', 1, 2};
		(void)parse_scan_response(short_response, 2);
	}, "wrong scan point count is rejected");
}

void test_gridlines()
{
	const auto columns = calculate_gridline_columns(87'500'000, 108'000'000, 2051, 6);
	check(!columns.empty(), "normal grid has columns");
	for(const auto column : columns)
		check(column >= 0 && column < 2051, "grid columns stay inside image bounds");

	const auto narrow = calculate_gridline_columns(100, 103, 4, 6);
	check(narrow == std::vector<std::int64_t>({0, 1, 2, 3}),
		"narrow grid uses signed in-range coordinates");
	check_throws([] {
		(void)calculate_gridline_columns(100, 101, 1, 6);
	}, "single-step grid is rejected");
}
}

int main()
{
	test_header_parsing();
	test_log_parsing();
	test_time_consistency();
	test_scan_response();
	test_gridlines();
	if(failures != 0)
	{
		std::cerr << std::format("{} test(s) failed\n", failures);
		return 1;
	}
	std::cout << "All tests passed\n";
	return 0;
}
