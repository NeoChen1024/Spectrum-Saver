#pragma once

#include "common.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

enum class LogFormat
{
	automatic,
	text,
	binary
};

struct LogFileMetadata
{
	std::uint64_t start_frequency_hz{};
	std::uint64_t stop_frequency_hz{};
	std::uint32_t resolution_bandwidth_hz{};
	std::uint32_t points_per_sweep{};
	std::int32_t calibration_scale_numerator{1};
	std::int32_t calibration_offset_numerator{};
	std::uint32_t calibration_denominator{1};
	std::chrono::sys_time<std::chrono::nanoseconds> creation_time{};
	std::string device_model;
	std::string writer_application;
	std::string device_identifier;
	std::string user_comment;
};

struct SweepRecord
{
	std::uint64_t sequence{};
	std::chrono::sys_time<std::chrono::nanoseconds> start_time{};
	std::chrono::sys_time<std::chrono::nanoseconds> end_time{};
	std::vector<std::uint16_t> samples;
};

struct LogReadResult
{
	LogFormat format{LogFormat::text};
	bool truncated_tail{};
	std::optional<LogFileMetadata> metadata;
};

[[nodiscard]] LogFormat parse_log_format(std::string_view value, bool allow_automatic);
[[nodiscard]] std::string_view log_format_name(LogFormat format);

class LogReader
{
public:
	virtual ~LogReader() = default;
	[[nodiscard]] virtual LogReadResult read(
		std::istream &input,
		std::vector<float> &power_data,
		std::vector<LogHeader> &headers
	) = 0;
};

class TextLogReader final : public LogReader
{
public:
	[[nodiscard]] LogReadResult read(
		std::istream &input,
		std::vector<float> &power_data,
		std::vector<LogHeader> &headers
	) override;
};

class BinaryLogReader final : public LogReader
{
public:
	[[nodiscard]] LogReadResult read(
		std::istream &input,
		std::vector<float> &power_data,
		std::vector<LogHeader> &headers
	) override;
};

[[nodiscard]] LogReadResult read_logfile(
	std::istream &input,
	LogFormat format,
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers
);

class LogWriter
{
public:
	virtual ~LogWriter() = default;
	virtual void write(SweepRecord record) = 0;
	virtual void close() = 0;
};

class TextLogWriter final : public LogWriter
{
public:
	TextLogWriter(std::ostream &output, LogFileMetadata metadata);
	void write(SweepRecord record) override;
	void close() override;

private:
	std::ostream &output_;
	LogFileMetadata metadata_;
	bool closed_{};
};

class BinaryLogWriter final : public LogWriter
{
public:
	BinaryLogWriter(std::ostream &output, LogFileMetadata metadata);
	void write(SweepRecord record) override;
	void close() override;

private:
	std::ostream &output_;
	LogFileMetadata metadata_;
	std::uint64_t next_sequence_{};
	bool closed_{};
};

class RotatingLogWriter final : public LogWriter
{
public:
	RotatingLogWriter(
		std::string filename_prefix,
		LogFormat format,
		LogFileMetadata metadata,
		std::size_t maximum_records
	);
	void write(SweepRecord record) override;
	void close() override;

private:
	void open_file(const SweepRecord &first_record);

	std::string filename_prefix_;
	LogFormat format_;
	LogFileMetadata metadata_;
	std::size_t maximum_records_;
	std::size_t record_count_{};
	std::unique_ptr<std::ofstream> output_;
	std::unique_ptr<LogWriter> writer_;
	bool closed_{};
};

class AsyncLogWriter
{
public:
	AsyncLogWriter(
		std::unique_ptr<LogWriter> writer,
		std::size_t maximum_queued_records,
		std::size_t maximum_queued_sample_bytes
	);
	~AsyncLogWriter();

	AsyncLogWriter(const AsyncLogWriter &) = delete;
	AsyncLogWriter &operator=(const AsyncLogWriter &) = delete;

	void enqueue(SweepRecord record);
	void check() const;
	void close();

private:
	[[nodiscard]] bool has_capacity(const SweepRecord &record) const noexcept;
	void rethrow_writer_error() const;
	void run() noexcept;

	std::unique_ptr<LogWriter> writer_;
	const std::size_t maximum_queued_records_;
	const std::size_t maximum_queued_sample_bytes_;
	std::deque<SweepRecord> queue_;
	std::size_t queued_sample_bytes_{};
	mutable std::mutex mutex_;
	std::condition_variable producer_condition_;
	std::condition_variable consumer_condition_;
	bool producer_closed_{};
	bool joined_{};
	std::exception_ptr writer_error_;
	std::jthread worker_;
};
