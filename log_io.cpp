#include "log_io.hpp"

#include <crc32c/crc32c.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <istream>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace
{
using Nanoseconds = std::chrono::nanoseconds;
using Timestamp = std::chrono::sys_time<Nanoseconds>;

constexpr std::array<std::uint8_t, 8> FILE_MAGIC{
	'S', 'P', 'S', 'L', 'O', 'G', '\r', '\n'};
constexpr std::array<std::uint8_t, 8> RECORD_MAGIC{
	'S', 'P', 'S', 'R', 'E', 'C', '\r', '\n'};
constexpr std::size_t FIXED_FILE_HEADER_BYTES = 72;
constexpr std::size_t MINIMUM_FILE_HEADER_BYTES = 76;
constexpr std::size_t RECORD_HEADER_BYTES = 48;
constexpr std::size_t CRC_BYTES = 4;
constexpr std::uint16_t FORMAT_MAJOR = 1;
constexpr std::uint16_t FORMAT_MINOR = 0;
constexpr std::uint16_t RAW_U16_AFFINE = 1;
constexpr std::size_t MAXIMUM_HEADER_BYTES = 1024 * 1024;
constexpr std::size_t MAXIMUM_RECORD_BYTES = 256 * 1024 * 1024;

template<typename T>
void append_integer(std::vector<std::uint8_t> &bytes, const T value)
{
	using Unsigned = std::make_unsigned_t<T>;
	const auto encoded = static_cast<Unsigned>(value);
	for(std::size_t index = 0; index < sizeof(T); ++index)
		bytes.emplace_back(static_cast<std::uint8_t>(encoded >> (index * 8)));
}

template<typename T>
void set_integer(std::vector<std::uint8_t> &bytes, const std::size_t offset, const T value)
{
	throw_if(offset > bytes.size() || bytes.size() - offset < sizeof(T),
		"Internal error: binary field offset is outside its buffer");
	using Unsigned = std::make_unsigned_t<T>;
	const auto encoded = static_cast<Unsigned>(value);
	for(std::size_t index = 0; index < sizeof(T); ++index)
		bytes[offset + index] = static_cast<std::uint8_t>(encoded >> (index * 8));
}

template<typename T>
[[nodiscard]] T read_integer(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
	throw_if(offset > bytes.size() || bytes.size() - offset < sizeof(T),
		"Binary log field extends beyond its containing structure");
	using Unsigned = std::make_unsigned_t<T>;
	Unsigned result{};
	for(std::size_t index = 0; index < sizeof(T); ++index)
		result |= static_cast<Unsigned>(bytes[offset + index]) << (index * 8);
	return static_cast<T>(result);
}

void append_bytes(std::vector<std::uint8_t> &target, const std::span<const std::uint8_t> source)
{
	target.insert(target.end(), source.begin(), source.end());
}

[[nodiscard]] bool valid_utf8(const std::span<const std::uint8_t> value)
{
	std::size_t offset = 0;
	while(offset < value.size())
	{
		const auto first = value[offset];
		if(first <= 0x7f)
		{
			++offset;
			continue;
		}
		std::size_t continuation_count;
		std::uint8_t second_min = 0x80;
		std::uint8_t second_max = 0xbf;
		if(first >= 0xc2 && first <= 0xdf)
			continuation_count = 1;
		else if(first >= 0xe0 && first <= 0xef)
		{
			continuation_count = 2;
			if(first == 0xe0)
				second_min = 0xa0;
			else if(first == 0xed)
				second_max = 0x9f;
		}
		else if(first >= 0xf0 && first <= 0xf4)
		{
			continuation_count = 3;
			if(first == 0xf0)
				second_min = 0x90;
			else if(first == 0xf4)
				second_max = 0x8f;
		}
		else
			return false;
		if(continuation_count >= value.size() - offset)
			return false;
		if(value[offset + 1] < second_min || value[offset + 1] > second_max)
			return false;
		for(std::size_t index = 2; index <= continuation_count; ++index)
		{
			if(value[offset + index] < 0x80 || value[offset + index] > 0xbf)
				return false;
		}
		offset += continuation_count + 1;
	}
	return true;
}

void append_tlv(
	std::vector<std::uint8_t> &header,
	const std::uint16_t type,
	const std::string_view value
)
{
	if(value.empty())
		return;
	const auto encoded_value = std::span{
		reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
	throw_if(value.back() == '\0' || !valid_utf8(encoded_value),
		"Binary log metadata must be valid UTF-8 without a trailing NUL");
	throw_if(value.size() > std::numeric_limits<std::uint32_t>::max(),
		"Binary log metadata value is too large");
	append_integer(header, type);
	append_integer<std::uint16_t>(header, 0);
	append_integer(header, static_cast<std::uint32_t>(value.size()));
	header.insert(header.end(), value.begin(), value.end());
}

[[nodiscard]] std::int64_t timestamp_count(const Timestamp timestamp)
{
	return timestamp.time_since_epoch().count();
}

[[nodiscard]] Timestamp timestamp_from_count(const std::int64_t count)
{
	return Timestamp{Nanoseconds{count}};
}

[[nodiscard]] std::string format_timestamp(const Timestamp timestamp)
{
	return std::format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(timestamp));
}

void write_bytes(std::ostream &output, const std::span<const std::uint8_t> bytes)
{
	throw_if(bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()),
		"Log write exceeds the stream size limit");
	if(!bytes.empty())
		output.write(reinterpret_cast<const char *>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	throw_if(!output, "Failed to write log data");
}

[[nodiscard]] bool read_exact_or_eof(
	std::istream &input,
	const std::span<std::uint8_t> destination
)
{
	if(destination.empty())
		return true;
	input.read(reinterpret_cast<char *>(destination.data()),
		static_cast<std::streamsize>(destination.size()));
	if(input.gcount() == 0 && input.eof())
		return false;
	throw_if(input.bad(), "Failed while reading binary log data");
	throw_if(input.gcount() != static_cast<std::streamsize>(destination.size()),
		"Truncated binary log structure");
	return true;
}

void read_exact(std::istream &input, const std::span<std::uint8_t> destination)
{
	throw_if(!read_exact_or_eof(input, destination), "Unexpected end of binary log");
}

void validate_metadata(const LogFileMetadata &metadata)
{
	throw_if(metadata.start_frequency_hz >= metadata.stop_frequency_hz,
		"Binary log start frequency must be lower than stop frequency");
	throw_if(metadata.resolution_bandwidth_hz == 0, "Binary log RBW must be nonzero");
	throw_if(metadata.points_per_sweep == 0, "Binary log point count must be nonzero");
	throw_if(metadata.calibration_denominator == 0,
		"Binary log calibration denominator must be nonzero");
	throw_if(metadata.calibration_scale_numerator == 0,
		"Binary log calibration scale numerator must be nonzero");
}

[[nodiscard]] std::vector<std::uint8_t> encode_file_header(const LogFileMetadata &metadata)
{
	validate_metadata(metadata);
	std::vector<std::uint8_t> header;
	header.reserve(MINIMUM_FILE_HEADER_BYTES + metadata.device_model.size()
		+ metadata.writer_application.size() + metadata.device_identifier.size()
		+ metadata.user_comment.size() + 32);
	append_bytes(header, FILE_MAGIC);
	append_integer(header, FORMAT_MAJOR);
	append_integer(header, FORMAT_MINOR);
	append_integer<std::uint32_t>(header, 0);
	append_integer<std::uint32_t>(header, 0);
	append_integer(header, RAW_U16_AFFINE);
	append_integer<std::uint16_t>(header, 0);
	append_integer(header, metadata.start_frequency_hz);
	append_integer(header, metadata.stop_frequency_hz);
	append_integer(header, metadata.resolution_bandwidth_hz);
	append_integer(header, metadata.points_per_sweep);
	append_integer(header, metadata.calibration_scale_numerator);
	append_integer(header, metadata.calibration_offset_numerator);
	append_integer(header, metadata.calibration_denominator);
	append_integer<std::uint32_t>(header, 0);
	append_integer(header, timestamp_count(metadata.creation_time));
	throw_if(header.size() != FIXED_FILE_HEADER_BYTES,
		"Internal error: incorrect fixed binary header size");
	append_tlv(header, 1, metadata.device_model);
	append_tlv(header, 2, metadata.writer_application);
	append_tlv(header, 3, metadata.device_identifier);
	append_tlv(header, 4, metadata.user_comment);
	throw_if(header.size() > MAXIMUM_HEADER_BYTES - CRC_BYTES,
		"Binary log header is too large");
	set_integer(header, 12, static_cast<std::uint32_t>(header.size() + CRC_BYTES));
	append_integer(header, crc32c::Crc32c(header.data(), header.size()));
	return header;
}

[[nodiscard]] std::string read_tlv_string(
	const std::span<const std::uint8_t> value,
	const std::uint16_t type
)
{
	throw_if((!value.empty() && value.back() == 0) || !valid_utf8(value),
		std::format("Binary metadata type {} is not valid non-NUL-terminated UTF-8", type));
	return {reinterpret_cast<const char *>(value.data()), value.size()};
}

[[nodiscard]] LogFileMetadata decode_file_header(std::istream &input)
{
	std::vector<std::uint8_t> header(FIXED_FILE_HEADER_BYTES);
	read_exact(input, header);
	throw_if(!std::ranges::equal(FILE_MAGIC, std::span{header}.first(FILE_MAGIC.size())),
		"Input does not have the Spectrum Saver binary log magic");
	throw_if(read_integer<std::uint16_t>(header, 8) != FORMAT_MAJOR,
		"Unsupported binary log major version");
	const auto minor = read_integer<std::uint16_t>(header, 10);
	const auto header_bytes = read_integer<std::uint32_t>(header, 12);
	throw_if(header_bytes < MINIMUM_FILE_HEADER_BYTES || header_bytes > MAXIMUM_HEADER_BYTES,
		"Invalid binary log header length");
	header.resize(header_bytes);
	read_exact(input, std::span{header}.subspan(FIXED_FILE_HEADER_BYTES));
	const auto stored_crc = read_integer<std::uint32_t>(header, header.size() - CRC_BYTES);
	const auto computed_crc = crc32c::Crc32c(header.data(), header.size() - CRC_BYTES);
	throw_if(stored_crc != computed_crc, "Binary log file header CRC32C mismatch");
	throw_if(read_integer<std::uint32_t>(header, 16) != 0,
		"Unsupported binary log file feature flags");
	throw_if(read_integer<std::uint16_t>(header, 20) != RAW_U16_AFFINE,
		"Unsupported binary log sample encoding");
	LogFileMetadata metadata{
		.start_frequency_hz = read_integer<std::uint64_t>(header, 24),
		.stop_frequency_hz = read_integer<std::uint64_t>(header, 32),
		.resolution_bandwidth_hz = read_integer<std::uint32_t>(header, 40),
		.points_per_sweep = read_integer<std::uint32_t>(header, 44),
		.calibration_scale_numerator = read_integer<std::int32_t>(header, 48),
		.calibration_offset_numerator = read_integer<std::int32_t>(header, 52),
		.calibration_denominator = read_integer<std::uint32_t>(header, 56),
		.creation_time = timestamp_from_count(read_integer<std::int64_t>(header, 64)),
		.device_model = {},
		.writer_application = {},
		.device_identifier = {},
		.user_comment = {}
	};
	validate_metadata(metadata);

	std::array<bool, 5> seen_types{};
	std::size_t offset = FIXED_FILE_HEADER_BYTES;
	const auto tlv_end = header.size() - CRC_BYTES;
	while(offset < tlv_end)
	{
		throw_if(tlv_end - offset < 8, "Truncated binary log metadata TLV header");
		const auto type = read_integer<std::uint16_t>(header, offset);
		const auto flags = read_integer<std::uint16_t>(header, offset + 2);
		const auto length = read_integer<std::uint32_t>(header, offset + 4);
		offset += 8;
		throw_if(length > tlv_end - offset, "Binary log metadata TLV extends beyond the header");
		throw_if((flags & ~std::uint16_t{1}) != 0, "Unsupported binary log metadata flags");
		const auto value = std::span{header}.subspan(offset, length);
		if(type >= 1 && type <= 4)
		{
			throw_if(seen_types[type], "Duplicate binary log metadata type");
			seen_types[type] = true;
			auto decoded = read_tlv_string(value, type);
			switch(type)
			{
				case 1: metadata.device_model = std::move(decoded); break;
				case 2: metadata.writer_application = std::move(decoded); break;
				case 3: metadata.device_identifier = std::move(decoded); break;
				case 4: metadata.user_comment = std::move(decoded); break;
			}
		}
		else
		{
			throw_if((flags & 1) != 0, "Unknown critical binary log metadata type");
		}
		offset += length;
	}
	throw_if(minor > FORMAT_MINOR && read_integer<std::uint32_t>(header, 16) != 0,
		"Unsupported newer binary log minor version features");
	return metadata;
}

[[nodiscard]] std::vector<std::uint8_t> encode_record_header(const SweepRecord &record)
{
	throw_if(record.samples.size() > std::numeric_limits<std::uint32_t>::max(),
		"Sweep has too many samples for the binary format");
	throw_if(record.end_time < record.start_time, "Sweep end time is earlier than start time");
	throw_if(record.samples.size() > (MAXIMUM_RECORD_BYTES
		- RECORD_HEADER_BYTES - CRC_BYTES) / 2, "Binary sweep record is too large");
	const auto payload_bytes = static_cast<std::uint32_t>(record.samples.size() * 2);
	const auto record_bytes = static_cast<std::uint32_t>(RECORD_HEADER_BYTES
		+ payload_bytes + CRC_BYTES);
	std::vector<std::uint8_t> header;
	header.reserve(RECORD_HEADER_BYTES);
	append_bytes(header, RECORD_MAGIC);
	append_integer(header, record_bytes);
	append_integer(header, static_cast<std::uint16_t>(RECORD_HEADER_BYTES));
	append_integer<std::uint16_t>(header, 0);
	append_integer(header, record.sequence);
	append_integer(header, timestamp_count(record.start_time));
	append_integer(header, timestamp_count(record.end_time));
	append_integer(header, static_cast<std::uint32_t>(record.samples.size()));
	append_integer(header, payload_bytes);
	return header;
}

[[nodiscard]] std::vector<std::uint8_t> encode_sample_payload(
	const std::span<const std::uint16_t> samples
)
{
	std::vector<std::uint8_t> payload;
	payload.reserve(samples.size() * 2);
	for(const auto sample : samples)
		append_integer(payload, sample);
	return payload;
}

[[nodiscard]] bool read_binary_records(
	std::istream &input,
	const LogFileMetadata &metadata,
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers
)
{
	std::uint64_t expected_sequence = 0;
	bool truncated_tail = false;
	std::vector<std::uint8_t> tail;
	while(true)
	{
		std::array<std::uint8_t, RECORD_HEADER_BYTES> record_header{};
		try
		{
			if(!read_exact_or_eof(input, record_header))
				break;
		}
		catch(const std::runtime_error &)
		{
			if(input.bad())
				throw;
			truncated_tail = true;
			break;
		}

		throw_if(!std::ranges::equal(RECORD_MAGIC,
			std::span{record_header}.first(RECORD_MAGIC.size())),
			"Binary log record synchronization marker mismatch");
		const auto record_bytes = read_integer<std::uint32_t>(record_header, 8);
		const auto record_header_bytes = read_integer<std::uint16_t>(record_header, 12);
		throw_if(record_header_bytes != RECORD_HEADER_BYTES,
			"Unsupported binary log record header length");
		throw_if(read_integer<std::uint16_t>(record_header, 14) != 0,
			"Unsupported binary log record flags");
		throw_if(record_bytes < RECORD_HEADER_BYTES + CRC_BYTES
			|| record_bytes > MAXIMUM_RECORD_BYTES, "Invalid binary log record length");
		const auto sequence = read_integer<std::uint64_t>(record_header, 16);
		throw_if(sequence != expected_sequence,
			std::format("Binary log sequence mismatch: expected {}, got {}",
				expected_sequence, sequence));
		const auto start_count = read_integer<std::int64_t>(record_header, 24);
		const auto end_count = read_integer<std::int64_t>(record_header, 32);
		throw_if(end_count < start_count, "Binary log record has a negative duration");
		const auto sample_count = read_integer<std::uint32_t>(record_header, 40);
		const auto payload_bytes = read_integer<std::uint32_t>(record_header, 44);
		throw_if(sample_count != metadata.points_per_sweep,
			"Binary log sample count does not match the file header");
		throw_if(sample_count > std::numeric_limits<std::uint32_t>::max() / 2
			|| payload_bytes != sample_count * 2,
			"Binary log payload length does not match its sample count");
		throw_if(record_bytes != RECORD_HEADER_BYTES + payload_bytes + CRC_BYTES,
			"Binary log total record length is inconsistent");

		tail.resize(payload_bytes + CRC_BYTES);
		try
		{
			read_exact(input, tail);
		}
		catch(const std::runtime_error &)
		{
			if(input.bad())
				throw;
			truncated_tail = true;
			break;
		}
		const auto stored_crc = read_integer<std::uint32_t>(tail, payload_bytes);
		auto computed_crc = crc32c::Crc32c(record_header.data(), record_header.size());
		computed_crc = crc32c::Extend(computed_crc, tail.data(), payload_bytes);
		throw_if(stored_crc != computed_crc, "Binary log record CRC32C mismatch");

		throw_if(power_data.size() > std::numeric_limits<std::size_t>::max() - sample_count,
			"Binary log decoded sample count overflows size_t");
		const auto required_capacity = power_data.size() + sample_count;
		if(required_capacity > power_data.capacity())
		{
			const auto growth = power_data.capacity() / 2;
			const auto grown_capacity = growth > std::numeric_limits<std::size_t>::max()
				- power_data.capacity() ? required_capacity : power_data.capacity() + growth;
			power_data.reserve(std::max(required_capacity, grown_capacity));
		}
		for(std::size_t offset = 0; offset < payload_bytes; offset += 2)
		{
			const auto raw = read_integer<std::uint16_t>(tail, offset);
			const auto numerator = static_cast<std::int64_t>(raw)
				* metadata.calibration_scale_numerator
				+ metadata.calibration_offset_numerator;
			power_data.emplace_back(static_cast<float>(numerator)
				/ static_cast<float>(metadata.calibration_denominator));
		}
		headers.emplace_back(LogHeader{
			.start_freq = static_cast<double>(metadata.start_frequency_hz) / 1e6,
			.stop_freq = static_cast<double>(metadata.stop_frequency_hz) / 1e6,
			.steps = sample_count,
			.rbw = static_cast<float>(metadata.resolution_bandwidth_hz) / 1e3F,
			.start_time = format_timestamp(timestamp_from_count(start_count)),
			.end_time = format_timestamp(timestamp_from_count(end_count))
		});
		++expected_sequence;
	}
	return truncated_tail;
}

[[nodiscard]] LogFormat detect_format(std::istream &input)
{
	const auto next = input.peek();
	throw_if(next == std::char_traits<char>::eof(), "Error: empty log file");
	return next == 'S' ? LogFormat::binary : LogFormat::text;
}
}

LogFormat parse_log_format(const std::string_view value, const bool allow_automatic)
{
	if(value == "text")
		return LogFormat::text;
	if(value == "binary")
		return LogFormat::binary;
	if(allow_automatic && value == "auto")
		return LogFormat::automatic;
	throw std::runtime_error(std::format("Invalid log format: {}", value));
}

std::string_view log_format_name(const LogFormat format)
{
	switch(format)
	{
		case LogFormat::automatic: return "auto";
		case LogFormat::text: return "text";
		case LogFormat::binary: return "binary";
	}
	throw std::runtime_error("Invalid log format value");
}

LogReadResult read_logfile(
	std::istream &input,
	LogFormat format,
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers
)
{
	throw_if(!input.good(), "Error: invalid logfile stream");
	if(format == LogFormat::automatic)
		format = detect_format(input);
	std::unique_ptr<LogReader> reader;
	if(format == LogFormat::text)
		reader = std::make_unique<TextLogReader>();
	else
		reader = std::make_unique<BinaryLogReader>();
	return reader->read(input, power_data, headers);
}

LogReadResult TextLogReader::read(
	std::istream &input,
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers
)
{
	parse_logfile(power_data, headers, input);
	return {
		.format = LogFormat::text,
		.truncated_tail = false,
		.metadata = std::nullopt
	};
}

LogReadResult BinaryLogReader::read(
	std::istream &input,
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers
)
{
	const auto metadata = decode_file_header(input);
	const bool truncated_tail = read_binary_records(input, metadata, power_data, headers);
	throw_if(headers.empty(), "Error: no valid record found in binary log file");
	return {
		.format = LogFormat::binary,
		.truncated_tail = truncated_tail,
		.metadata = metadata
	};
}

TextLogWriter::TextLogWriter(std::ostream &output, LogFileMetadata metadata)
	: output_{output}, metadata_{std::move(metadata)}
{
	validate_metadata(metadata_);
}

void TextLogWriter::write(SweepRecord record)
{
	throw_if(closed_, "Cannot write to a closed text log writer");
	throw_if(record.samples.size() != metadata_.points_per_sweep,
		"Sweep sample count does not match the log metadata");
	throw_if(record.end_time < record.start_time, "Sweep end time is earlier than start time");
	output_ << std::format("$ {:.06f},{:.06f},{},{:.03f},{},{}\n",
		static_cast<double>(metadata_.start_frequency_hz) / 1e6,
		static_cast<double>(metadata_.stop_frequency_hz) / 1e6,
		metadata_.points_per_sweep,
		static_cast<double>(metadata_.resolution_bandwidth_hz) / 1e3,
		format_timestamp(record.start_time), format_timestamp(record.end_time));
	for(const auto raw_sample : record.samples)
	{
		const auto numerator = static_cast<std::int64_t>(raw_sample)
			* metadata_.calibration_scale_numerator
			+ metadata_.calibration_offset_numerator;
		output_ << std::format("{:.1f}\n", static_cast<double>(numerator)
			/ metadata_.calibration_denominator);
	}
	output_ << '\n';
	throw_if(!output_, "Failed to write text log record");
}

void TextLogWriter::close()
{
	if(closed_)
		return;
	output_.flush();
	throw_if(!output_, "Failed to flush text log");
	closed_ = true;
}

BinaryLogWriter::BinaryLogWriter(std::ostream &output, LogFileMetadata metadata)
	: output_{output}, metadata_{std::move(metadata)}
{
	write_bytes(output_, encode_file_header(metadata_));
}

void BinaryLogWriter::write(SweepRecord record)
{
	throw_if(closed_, "Cannot write to a closed binary log writer");
	throw_if(record.sequence != next_sequence_,
		std::format("Binary record sequence mismatch: expected {}, got {}",
			next_sequence_, record.sequence));
	throw_if(record.samples.size() != metadata_.points_per_sweep,
		"Sweep sample count does not match the binary log metadata");
	const auto header = encode_record_header(record);
	const auto payload = encode_sample_payload(record.samples);
	auto crc = crc32c::Crc32c(header.data(), header.size());
	crc = crc32c::Extend(crc, payload.data(), payload.size());
	write_bytes(output_, header);
	write_bytes(output_, payload);
	std::vector<std::uint8_t> encoded_crc;
	encoded_crc.reserve(CRC_BYTES);
	append_integer(encoded_crc, crc);
	write_bytes(output_, encoded_crc);
	++next_sequence_;
}

void BinaryLogWriter::close()
{
	if(closed_)
		return;
	output_.flush();
	throw_if(!output_, "Failed to flush binary log");
	closed_ = true;
}

RotatingLogWriter::RotatingLogWriter(
	std::string filename_prefix,
	const LogFormat format,
	LogFileMetadata metadata,
	const std::size_t maximum_records
) : filename_prefix_{std::move(filename_prefix)}, format_{format},
	metadata_{std::move(metadata)}, maximum_records_{maximum_records}
{
	throw_if(format_ == LogFormat::automatic, "Automatic format is invalid for log output");
	validate_metadata(metadata_);
}

void RotatingLogWriter::open_file(const SweepRecord &first_record)
{
	if(writer_)
		writer_->close();
	writer_.reset();
	if(output_)
	{
		output_->close();
		throw_if(output_->fail(), "Failed to close rotated log file");
	}
	const auto timestamp = format_timestamp(first_record.start_time);
	const auto extension = format_ == LogFormat::binary ? "splog" : "log";
	const auto filename = std::format("{}.{}.{}", filename_prefix_, timestamp, extension);
	output_ = std::make_unique<std::ofstream>(filename,
		std::ios::out | (format_ == LogFormat::binary ? std::ios::binary : std::ios::openmode{}));
	throw_if(!output_->is_open(), std::format("Error: cannot open output file: {}", filename));
	metadata_.creation_time = first_record.start_time;
	if(format_ == LogFormat::binary)
		writer_ = std::make_unique<BinaryLogWriter>(*output_, metadata_);
	else
		writer_ = std::make_unique<TextLogWriter>(*output_, metadata_);
	record_count_ = 0;
	print("\nOpened log file: {}\n", filename);
}

void RotatingLogWriter::write(SweepRecord record)
{
	throw_if(closed_, "Cannot write to a closed rotating log writer");
	if(!writer_ || (maximum_records_ != 0 && record_count_ >= maximum_records_))
		open_file(record);
	record.sequence = record_count_;
	writer_->write(std::move(record));
	++record_count_;
}

void RotatingLogWriter::close()
{
	if(closed_)
		return;
	if(writer_)
		writer_->close();
	writer_.reset();
	if(output_)
	{
		output_->close();
		throw_if(output_->fail(), "Failed to close log file");
	}
	closed_ = true;
}

AsyncLogWriter::AsyncLogWriter(
	std::unique_ptr<LogWriter> writer,
	const std::size_t maximum_queued_records,
	const std::size_t maximum_queued_sample_bytes
) : writer_{std::move(writer)}, maximum_queued_records_{maximum_queued_records},
	maximum_queued_sample_bytes_{maximum_queued_sample_bytes}
{
	throw_if(!writer_, "Async log writer requires an underlying writer");
	throw_if(maximum_queued_records_ == 0 || maximum_queued_sample_bytes_ == 0,
		"Async log writer queue limits must be nonzero");
	worker_ = std::jthread{[this] { run(); }};
}

AsyncLogWriter::~AsyncLogWriter()
{
	try
	{
		close();
	}
	catch(...)
	{
	}
}

bool AsyncLogWriter::has_capacity(const SweepRecord &record) const noexcept
{
	const auto sample_bytes = record.samples.size() * sizeof(std::uint16_t);
	return queue_.size() < maximum_queued_records_
		&& sample_bytes <= maximum_queued_sample_bytes_ - queued_sample_bytes_;
}

void AsyncLogWriter::rethrow_writer_error() const
{
	if(writer_error_)
		std::rethrow_exception(writer_error_);
}

void AsyncLogWriter::enqueue(SweepRecord record)
{
	throw_if(record.samples.size() > maximum_queued_sample_bytes_ / sizeof(std::uint16_t),
		"Sweep exceeds the asynchronous writer queue byte limit");
	std::unique_lock lock{mutex_};
	producer_condition_.wait(lock, [this, &record] {
		return producer_closed_ || writer_error_ || has_capacity(record);
	});
	rethrow_writer_error();
	throw_if(producer_closed_, "Cannot enqueue into a closed asynchronous log writer");
	queued_sample_bytes_ += record.samples.size() * sizeof(std::uint16_t);
	queue_.emplace_back(std::move(record));
	consumer_condition_.notify_one();
}

void AsyncLogWriter::check() const
{
	std::lock_guard lock{mutex_};
	rethrow_writer_error();
	throw_if(producer_closed_, "Asynchronous log writer is closed");
}

void AsyncLogWriter::close()
{
	{
		std::lock_guard lock{mutex_};
		if(!producer_closed_)
			producer_closed_ = true;
	}
	consumer_condition_.notify_all();
	producer_condition_.notify_all();
	if(!joined_)
	{
		worker_.join();
		joined_ = true;
	}
	std::lock_guard lock{mutex_};
	rethrow_writer_error();
}

void AsyncLogWriter::run() noexcept
{
	try
	{
		while(true)
		{
			SweepRecord record;
			{
				std::unique_lock lock{mutex_};
				consumer_condition_.wait(lock, [this] {
					return producer_closed_ || !queue_.empty();
				});
				if(queue_.empty())
					break;
				record = std::move(queue_.front());
				queue_.pop_front();
				queued_sample_bytes_ -= record.samples.size() * sizeof(std::uint16_t);
				producer_condition_.notify_all();
			}
			writer_->write(std::move(record));
		}
		writer_->close();
	}
	catch(...)
	{
		std::lock_guard lock{mutex_};
		writer_error_ = std::current_exception();
		producer_closed_ = true;
		queue_.clear();
		queued_sample_bytes_ = 0;
		producer_condition_.notify_all();
		consumer_condition_.notify_all();
	}
}
