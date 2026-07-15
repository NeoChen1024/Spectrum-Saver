#include "common.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace
{
[[nodiscard]] std::string_view trim(std::string_view value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if(first == std::string_view::npos)
		return {};
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

template<typename T>
[[nodiscard]] bool parse_number(std::string_view text, T &value)
{
	text = trim(text);
	if(text.empty())
		return false;

	bool explicitly_positive = false;
	if(text.front() == '+')
	{
		explicitly_positive = true;
		text.remove_prefix(1);
		if(text.empty())
			return false;
	}

	T parsed{};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if(error != std::errc{} || end != text.data() + text.size())
		return false;
	if constexpr(std::is_floating_point_v<T>)
	{
		if(!std::isfinite(parsed))
			return false;
	}
	if constexpr(std::is_signed_v<T>)
	{
		if(explicitly_positive && parsed < 0)
			return false;
	}

	value = parsed;
	return true;
}

[[nodiscard]] bool split_header_fields(
	std::string_view line,
	std::array<std::string_view, 6> &fields
)
{
	if(line.empty() || line.front() != '$')
		return false;
	line.remove_prefix(1);

	for(std::size_t index = 0; index < fields.size(); ++index)
	{
		const auto comma = line.find(',');
		if(index + 1 == fields.size())
		{
			if(comma != std::string_view::npos)
				return false;
			fields[index] = trim(line);
			return !fields[index].empty();
		}
		if(comma == std::string_view::npos)
			return false;
		fields[index] = trim(line.substr(0, comma));
		if(fields[index].empty())
			return false;
		line.remove_prefix(comma + 1);
	}
	return false;
}

[[nodiscard]] std::int64_t checked_size_to_int64(const std::size_t value, std::string_view name)
{
	throw_if(value > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()),
		std::format("{} is too large", name));
	return static_cast<std::int64_t>(value);
}
}

void throw_if(const bool condition, const std::string_view message)
{
	if(condition)
		throw std::runtime_error(std::string{message});
}

std::chrono::system_clock::time_point now()
{
	return std::chrono::system_clock::now();
}

std::string time_str()
{
	return std::format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(now()));
}

std::chrono::sys_seconds time_from_str(const std::string_view str)
{
	std::chrono::sys_seconds time;
	std::istringstream stream{std::string{str}};
	stream >> std::chrono::parse("%4Y%2m%2dT%2H%2M%2S", time);
	throw_if(stream.fail() || stream.peek() != std::char_traits<char>::eof(),
		std::format("Failed to parse time string: \"{}\"", str));
	return time;
}

bool parse_header(const std::string_view line, LogHeader &header)
{
	std::array<std::string_view, 6> fields;
	if(!split_header_fields(line, fields))
		return false;

	LogHeader parsed;
	if(!parse_number(fields[0], parsed.start_freq)
		|| !parse_number(fields[1], parsed.stop_freq)
		|| !parse_number(fields[2], parsed.steps)
		|| !parse_number(fields[3], parsed.rbw))
		return false;
	parsed.start_time = fields[4];
	parsed.end_time = fields[5];

	if(parsed.start_freq >= parsed.stop_freq || parsed.steps == 0
		|| parsed.rbw <= 0 || parsed.rbw > 1000)
		return false;

	try
	{
		(void)time_from_str(parsed.start_time);
		(void)time_from_str(parsed.end_time);
	}
	catch(const std::runtime_error &)
	{
		return false;
	}

	header = std::move(parsed);
	return true;
}

void parse_logfile(
	std::vector<float> &power_data,
	std::vector<LogHeader> &headers,
	std::istream &logfile_stream
)
{
	enum class ParseState
	{
		header,
		samples,
		separator
	};

	throw_if(!logfile_stream.good(), "Error: invalid logfile stream");

	std::optional<LogHeader> reference_header;
	if(!headers.empty())
		reference_header = headers.front();
	ParseState state = ParseState::header;
	std::size_t samples_in_record = 0;
	std::size_t real_line_count = 0;
	std::string line;

	while(std::getline(logfile_stream, line))
	{
		++real_line_count;
		if(!line.empty() && line.front() == '#')
			continue;

		switch(state)
		{
			case ParseState::header:
			{
				LogHeader header;
				throw_if(!parse_header(line, header),
					std::format("Error: invalid header at line #{}", real_line_count));

				if(!reference_header)
				{
					reference_header = header;
					headers.emplace_back(std::move(header));
				}
				else
				{
					throw_if(header.start_freq != reference_header->start_freq,
						std::format("Error: start_freq mismatch at line #{}: {} != {}",
							real_line_count, header.start_freq, reference_header->start_freq));
					throw_if(header.stop_freq != reference_header->stop_freq,
						std::format("Error: stop_freq mismatch at line #{}: {} != {}",
							real_line_count, header.stop_freq, reference_header->stop_freq));
					throw_if(header.steps != reference_header->steps,
						std::format("Error: steps count mismatch at line #{}: {} != {}",
							real_line_count, header.steps, reference_header->steps));
					throw_if(header.rbw != reference_header->rbw,
						std::format("Error: rbw mismatch at line #{}: {} != {}",
							real_line_count, header.rbw, reference_header->rbw));
					headers.emplace_back(std::move(header));
				}

				samples_in_record = 0;
				state = ParseState::samples;
				break;
			}
			case ParseState::samples:
			{
				float power{};
				throw_if(!parse_number(line, power),
					std::format("Error: invalid power value at line #{}: \"{}\"",
						real_line_count, line));
				power_data.emplace_back(power);
				++samples_in_record;
				if(samples_in_record == reference_header->steps)
					state = ParseState::separator;
				break;
			}
			case ParseState::separator:
				throw_if(!line.empty(),
					std::format("Error: blank separator expected at line #{}", real_line_count));
				state = ParseState::header;
				break;
		}
	}

	throw_if(logfile_stream.bad(), "Error: failed while reading logfile stream");
	throw_if(headers.empty(), "Error: no valid record found in log file");
	throw_if(state == ParseState::samples,
		std::format("Error: incomplete final record: expected {} samples, got {}",
			reference_header->steps, samples_in_record));
	throw_if(headers.size() > std::numeric_limits<std::size_t>::max() / reference_header->steps,
		"Error: expected power_data count overflows size_t");
	throw_if(power_data.size() != headers.size() * reference_header->steps,
		"Error: power_data count is not correct");
}

bool check_logfile_time_consistency(
	const std::span<const LogHeader> headers,
	LogProblems &problems
)
{
	problems = {};
	std::size_t inconsistency_count = 0;
	if(headers.empty())
		return false;

	for(std::size_t index = 0; index < headers.size(); ++index)
	{
		const auto start = time_from_str(headers[index].start_time);
		const auto end = time_from_str(headers[index].end_time);
		if(end < start)
		{
			std::cerr << std::format(
				"Warning: end time is earlier than start time in record #{}\n", index + 1);
			problems.time_overlap = true;
			++inconsistency_count;
		}
	}

	if(headers.size() == 1)
		return inconsistency_count != 0;

	const auto first_sweep_time = time_from_str(headers.front().start_time);
	const auto last_sweep_time = time_from_str(headers.back().start_time);
	const auto interval_count = checked_size_to_int64(headers.size() - 1, "record interval count");
	const auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(
		last_sweep_time - first_sweep_time).count();
	const auto nominal_interval = time_diff / interval_count;

	if(time_diff % interval_count != 0)
	{
		std::cerr << std::format(
			"Warning: time range in seconds ({}) is not divisible by interval count ({})\n",
			time_diff, interval_count);
		problems.time_range_not_divisible_by_record_count = true;
		++inconsistency_count;
	}

	if(nominal_interval > 0 && 60 % nominal_interval != 0)
	{
		std::cerr << std::format(
			"Warning: time interval {}sec is not a factor of 60\n", nominal_interval);
		problems.interval_not_divisible_by_60 = true;
		++inconsistency_count;
	}
	else if(nominal_interval <= 0)
	{
		problems.negative_interval = true;
		++inconsistency_count;
	}

	std::int64_t last_interval = nominal_interval;
	for(std::size_t index = 0; index + 1 < headers.size(); ++index)
	{
		const auto start = time_from_str(headers[index].start_time);
		const auto end = time_from_str(headers[index].end_time);
		const auto next_start = time_from_str(headers[index + 1].start_time);
		const auto difference = std::chrono::duration_cast<std::chrono::seconds>(
			next_start - start).count();

		if(end > next_start || start >= next_start)
		{
			std::cerr << std::format(
				"Warning: timestamp overlap between record #{} and #{}\n", index + 1, index + 2);
			problems.time_overlap = true;
			++inconsistency_count;
		}
		if(difference != last_interval)
		{
			std::cerr << std::format(
				"Warning: interval between record #{} and #{} changed from {}s to {}s\n",
				index + 1, index + 2, last_interval, difference);
			problems.variant_interval = true;
			++inconsistency_count;
		}
		if(difference <= 0)
		{
			std::cerr << std::format(
				"Warning: non-positive interval between record #{} and #{}\n",
				index + 1, index + 2);
			problems.negative_interval = true;
			++inconsistency_count;
		}
		last_interval = difference;
	}

	if(inconsistency_count != 0)
	{
		std::cerr << std::format(
			"{} inconsistency(s) found, may not be able to perform time-based operations correctly.\n",
			inconsistency_count);
	}
	return inconsistency_count != 0;
}

std::vector<std::int64_t> calculate_gridline_columns(
	const std::int64_t start_frequency_hz,
	const std::int64_t stop_frequency_hz,
	const std::size_t steps,
	const std::size_t minimum_gridlines
)
{
	throw_if(start_frequency_hz < 0 || stop_frequency_hz <= start_frequency_hz,
		"Invalid frequency range for gridlines");
	throw_if(steps < 2, "At least two frequency steps are required for gridlines");
	throw_if(minimum_gridlines == 0, "Minimum gridline count must be positive");

	const auto step_count = checked_size_to_int64(steps - 1, "frequency step count");
	const auto frequency_range = stop_frequency_hz - start_frequency_hz;
	std::int64_t exponent = 100'000'000'000;
	std::int64_t spacing = 1;
	bool found_spacing = false;

	while(exponent > 0 && !found_spacing)
	{
		for(const std::int64_t multiplier : {5, 2, 1})
		{
			if(exponent > std::numeric_limits<std::int64_t>::max() / multiplier)
				continue;
			const auto candidate = exponent * multiplier;
			if(candidate > 0
				&& frequency_range / candidate >= checked_size_to_int64(minimum_gridlines, "minimum gridline count"))
			{
				spacing = candidate;
				found_spacing = true;
				break;
			}
		}
		exponent /= 10;
	}

	const auto quotient = start_frequency_hz / spacing;
	throw_if(quotient == std::numeric_limits<std::int64_t>::max(),
		"Gridline frequency overflow");
	std::int64_t frequency = quotient * spacing;
	if(frequency < start_frequency_hz)
		frequency += spacing;

	std::vector<std::int64_t> columns;
	for(; frequency <= stop_frequency_hz; )
	{
		const auto offset = frequency - start_frequency_hz;
		throw_if(offset != 0 && step_count > std::numeric_limits<std::int64_t>::max() / offset,
			"Gridline coordinate overflow");
		columns.emplace_back((offset * step_count + frequency_range / 2) / frequency_range);
		if(frequency > stop_frequency_hz - spacing)
			break;
		frequency += spacing;
	}
	return columns;
}
