#include "common.hpp"
#include "log_io.hpp"
#include "serial_protocol.hpp"

#include <crc32c/crc32c.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
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

[[nodiscard]] auto test_timestamp(const std::string_view value)
{
	return std::chrono::time_point_cast<std::chrono::nanoseconds>(time_from_str(value));
}

[[nodiscard]] LogFileMetadata test_metadata()
{
	return {
		.start_frequency_hz = 1'000'000,
		.stop_frequency_hz = 2'000'000,
		.resolution_bandwidth_hz = 10'000,
		.points_per_sweep = 2,
		.calibration_scale_numerator = 1,
		.calibration_offset_numerator = -4096,
		.calibration_denominator = 32,
		.creation_time = test_timestamp("20230101T000000"),
		.device_model = {},
		.writer_application = {},
		.device_identifier = {},
		.user_comment = {}
	};
}

[[nodiscard]] SweepRecord test_record(
	const std::uint64_t sequence,
	const std::string_view start,
	const std::string_view end,
	std::vector<std::uint16_t> samples
)
{
	return {
		.sequence = sequence,
		.start_time = test_timestamp(start),
		.end_time = test_timestamp(end),
		.samples = std::move(samples)
	};
}

[[nodiscard]] std::uint32_t read_u32(const std::string &bytes, const std::size_t offset)
{
	check(offset <= bytes.size() && bytes.size() - offset >= 4,
		"test fixture uint32 offset is valid");
	return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset]))
		| static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8
		| static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16
		| static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24;
}

void write_u32(std::string &bytes, const std::size_t offset, const std::uint32_t value)
{
	check(offset <= bytes.size() && bytes.size() - offset >= 4,
		"test fixture uint32 write offset is valid");
	for(std::size_t index = 0; index < 4; ++index)
		bytes[offset + index] = static_cast<char>(value >> (index * 8));
}

void update_crc(std::string &bytes, const std::size_t start, const std::size_t crc_offset)
{
	const auto crc = crc32c::Crc32c(
		reinterpret_cast<const std::uint8_t *>(bytes.data() + start), crc_offset - start);
	write_u32(bytes, crc_offset, crc);
}

void test_binary_log()
{
	const std::string crc_input{"123456789"};
	check(crc32c::Crc32c(crc_input) == 0xe3069283U,
		"google/crc32c matches the standard check value");

	std::ostringstream output{std::ios::out | std::ios::binary};
	BinaryLogWriter writer{output, test_metadata()};
	writer.write(test_record(0, "20230101T000000", "20230101T000001", {4096, 4064}));
	writer.write(test_record(1, "20230101T000100", "20230101T000101", {4032, 4000}));
	writer.close();
	const auto encoded = output.str();
	constexpr std::array<std::uint8_t, 76> golden_header{
		0x53, 0x50, 0x53, 0x4c, 0x4f, 0x47, 0x0d, 0x0a, 0x01, 0x00, 0x00, 0x00,
		0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x40, 0x42, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x84, 0x1e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xff, 0xff, 0x20, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc2, 0xd3, 0x43, 0x06, 0x36, 0x17,
		0xb4, 0x2a, 0xcb, 0xf4
	};
	check(std::equal(golden_header.begin(), golden_header.end(),
		reinterpret_cast<const std::uint8_t *>(encoded.data())),
		"binary file header matches the independent golden byte fixture");
	check(encoded.size() == 76 + 2 * 56, "binary file and record sizes match the specification");
	check(encoded.substr(0, 8) == std::string{"SPSLOG\r\n"}, "binary file magic is exact");
	check(read_u32(encoded, 12) == 76, "binary file header length is exact");
	check(encoded.substr(76, 8) == std::string{"SPSREC\r\n"}, "binary record magic is exact");
	check(read_u32(encoded, 76 + 8) == 56, "binary record length is exact");
	check(read_u32(encoded, 76 + 40) == 2 && read_u32(encoded, 76 + 44) == 4,
		"binary sample count and payload length are exact");

	std::istringstream input{encoded, std::ios::in | std::ios::binary};
	std::vector<float> power;
	std::vector<LogHeader> headers;
	const auto result = read_logfile(input, LogFormat::automatic, power, headers);
	check(result.format == LogFormat::binary && !result.truncated_tail,
		"binary log is auto-detected and complete");
	check(headers.size() == 2 && power == std::vector<float>({0.0F, -1.0F, -2.0F, -3.0F}),
		"binary records decode with exact affine calibration");
	check(headers.front().start_freq == 1.0 && headers.front().rbw == 10.0F,
		"binary file metadata converts to the renderer header");

	const auto truncated = encoded.substr(0, encoded.size() - 2);
	std::istringstream truncated_input{truncated, std::ios::in | std::ios::binary};
	power.clear();
	headers.clear();
	const auto truncated_result = read_logfile(
		truncated_input, LogFormat::binary, power, headers);
	check(truncated_result.truncated_tail && headers.size() == 1 && power.size() == 2,
		"truncated final record preserves all preceding records");

	auto corrupted = encoded;
	corrupted[76 + 48] ^= 1;
	check_throws([&corrupted] {
		std::istringstream corrupt_input{corrupted, std::ios::in | std::ios::binary};
		std::vector<float> values;
		std::vector<LogHeader> parsed_headers;
		(void)read_logfile(corrupt_input, LogFormat::binary, values, parsed_headers);
	}, "binary record CRC32C corruption is rejected");

	auto bad_header_crc = encoded;
	bad_header_crc[24] ^= 1;
	check_throws([&bad_header_crc] {
		std::istringstream bad_input{bad_header_crc, std::ios::in | std::ios::binary};
		std::vector<float> values;
		std::vector<LogHeader> parsed_headers;
		(void)read_logfile(bad_input, LogFormat::binary, values, parsed_headers);
	}, "binary file header CRC32C corruption is rejected");

	auto unknown_flags = encoded;
	unknown_flags[16] = 1;
	update_crc(unknown_flags, 0, 72);
	check_throws([&unknown_flags] {
		std::istringstream bad_input{unknown_flags, std::ios::in | std::ios::binary};
		std::vector<float> values;
		std::vector<LogHeader> parsed_headers;
		(void)read_logfile(bad_input, LogFormat::binary, values, parsed_headers);
	}, "unknown binary file feature flags are rejected after CRC validation");

	auto bad_sequence = encoded;
	bad_sequence[76 + 16] = 1;
	update_crc(bad_sequence, 76, 76 + 52);
	check_throws([&bad_sequence] {
		std::istringstream bad_input{bad_sequence, std::ios::in | std::ios::binary};
		std::vector<float> values;
		std::vector<LogHeader> parsed_headers;
		(void)read_logfile(bad_input, LogFormat::binary, values, parsed_headers);
	}, "binary sequence discontinuity is rejected");

	auto metadata = test_metadata();
	metadata.device_model = "tinySA4";
	metadata.user_comment = "fixture";
	std::ostringstream metadata_output{std::ios::out | std::ios::binary};
	BinaryLogWriter metadata_writer{metadata_output, metadata};
	metadata_writer.write(test_record(0, "20230101T000000", "20230101T000001", {4096, 4064}));
	metadata_writer.close();
	std::istringstream metadata_input{metadata_output.str(), std::ios::in | std::ios::binary};
	power.clear();
	headers.clear();
	const auto metadata_result = read_logfile(
		metadata_input, LogFormat::binary, power, headers);
	check(metadata_result.metadata && metadata_result.metadata->device_model == "tinySA4"
		&& metadata_result.metadata->user_comment == "fixture",
		"binary metadata TLVs round trip");

	check_throws([] {
		auto invalid_metadata = test_metadata();
		invalid_metadata.device_model = std::string(1, static_cast<char>(0xff));
		std::ostringstream invalid_output{std::ios::out | std::ios::binary};
		BinaryLogWriter invalid_writer{invalid_output, std::move(invalid_metadata)};
	}, "invalid UTF-8 metadata is rejected by the binary writer");
}

void test_text_writer()
{
	std::ostringstream output;
	TextLogWriter writer{output, test_metadata()};
	writer.write(test_record(0, "20230101T000000", "20230101T000001", {4096, 4064}));
	writer.close();
	std::istringstream input{output.str()};
	std::vector<float> power;
	std::vector<LogHeader> headers;
	const auto result = read_logfile(input, LogFormat::automatic, power, headers);
	check(result.format == LogFormat::text && headers.size() == 1,
		"text writer remains compatible with the text reader");
	check(power == std::vector<float>({0.0F, -1.0F}),
		"text writer uses the shared affine calibration");
}

struct WriterState
{
	std::vector<std::vector<std::uint16_t>> records;
	bool closed{};
};

class CollectingWriter final : public LogWriter
{
public:
	explicit CollectingWriter(std::shared_ptr<WriterState> state) : state_{std::move(state)} {}
	void write(SweepRecord record) override
	{
		state_->records.emplace_back(std::move(record.samples));
	}
	void close() override { state_->closed = true; }

private:
	std::shared_ptr<WriterState> state_;
};

class FailingWriter final : public LogWriter
{
public:
	void write(SweepRecord) override { throw std::runtime_error("intentional writer failure"); }
	void close() override {}
};

void test_async_writer()
{
	auto state = std::make_shared<WriterState>();
	AsyncLogWriter writer{std::make_unique<CollectingWriter>(state), 2, 16};
	writer.enqueue(test_record(0, "20230101T000000", "20230101T000001", {1, 2}));
	writer.enqueue(test_record(1, "20230101T000100", "20230101T000101", {3, 4}));
	writer.close();
	check(state->closed && state->records == std::vector<std::vector<std::uint16_t>>({{1, 2}, {3, 4}}),
		"asynchronous writer drains accepted records in order");

	AsyncLogWriter failing{std::make_unique<FailingWriter>(), 1, 8};
	failing.enqueue(test_record(0, "20230101T000000", "20230101T000001", {1}));
	check_throws([&failing] { failing.close(); },
		"asynchronous writer propagates the underlying writer error");
}
}

int main()
{
	test_header_parsing();
	test_log_parsing();
	test_time_consistency();
	test_scan_response();
	test_gridlines();
	test_binary_log();
	test_text_writer();
	test_async_writer();
	if(failures != 0)
	{
		std::cerr << std::format("{} test(s) failed\n", failures);
		return 1;
	}
	std::cout << "All tests passed\n";
	return 0;
}
