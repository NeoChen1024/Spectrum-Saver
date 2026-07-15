/*
 *   splogconvert - convert Spectrum Saver text and binary logs
 *   Copyright (C) 2026 Kelei Chen
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#include "common.hpp"
#include "config.hpp"
#include "log_io.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct Options
{
	std::string input_name;
	std::string output_name;
	LogFormat input_format{LogFormat::automatic};
	LogFormat output_format{LogFormat::automatic};
	std::optional<std::string> model;
};

void print_help(const char *program)
{
	std::cout << "Usage: " << program << " -f <input> -o <output> "
		"--output-format text|binary [--input-format auto|text|binary] "
		"[--model tinySA|tinySA4]\n";
}

[[nodiscard]] Options parse_arguments(const int argc, char *argv[])
{
	Options options;
	const option long_options[]{
		{"input-format", required_argument, nullptr, 'I'},
		{"output-format", required_argument, nullptr, 'O'},
		{"model", required_argument, nullptr, 'm'},
		{"help", no_argument, nullptr, 'h'},
		{nullptr, 0, nullptr, 0}
	};
	int argument;
	while((argument = ::getopt_long(argc, argv, "f:o:I:O:m:h", long_options, nullptr)) != -1)
	{
		switch(argument)
		{
			case 'f': options.input_name = optarg; break;
			case 'o': options.output_name = optarg; break;
			case 'I': options.input_format = parse_log_format(optarg, true); break;
			case 'O': options.output_format = parse_log_format(optarg, false); break;
			case 'm': options.model = optarg; break;
			case 'h': print_help(argv[0]); std::exit(EXIT_SUCCESS);
			default: throw std::runtime_error("Invalid command-line arguments");
		}
	}
	throw_if(options.input_name.empty(), "Error: no input log specified (-f)");
	throw_if(options.output_name.empty(), "Error: no output log specified (-o)");
	throw_if(options.output_format == LogFormat::automatic,
		"Error: --output-format text|binary is required");
	throw_if(options.input_name == "-" && options.output_name == "-",
		"Error: input and output cannot both use standard streams");
	return options;
}

[[nodiscard]] std::uint64_t frequency_to_hz(const double frequency_mhz)
{
	throw_if(!std::isfinite(frequency_mhz) || frequency_mhz < 0
		|| frequency_mhz > static_cast<double>(std::numeric_limits<std::uint64_t>::max()) / 1e6,
		"Frequency is outside the binary format range");
	return static_cast<std::uint64_t>(std::llround(frequency_mhz * 1e6));
}

[[nodiscard]] LogFileMetadata metadata_from_text(
	const std::span<const LogHeader> headers,
	const std::string_view model
)
{
	throw_if(headers.empty(), "Cannot derive binary metadata from an empty text log");
	int zero_level;
	if(model == "tinySA")
		zero_level = ZERO_LEVEL;
	else if(model == "tinySA4")
		zero_level = ZERO_LEVEL_ULTRA;
	else
		throw std::runtime_error(std::format("Unknown tinySA model: {}", model));
	const auto &header = headers.front();
	throw_if(header.steps > std::numeric_limits<std::uint32_t>::max(),
		"Point count exceeds the binary format range");
	throw_if(!std::isfinite(header.rbw) || header.rbw <= 0
		|| header.rbw * 1e3F > std::numeric_limits<std::uint32_t>::max(),
		"RBW is outside the binary format range");
	return {
		.start_frequency_hz = frequency_to_hz(header.start_freq),
		.stop_frequency_hz = frequency_to_hz(header.stop_freq),
		.resolution_bandwidth_hz = static_cast<std::uint32_t>(std::llround(header.rbw * 1e3F)),
		.points_per_sweep = static_cast<std::uint32_t>(header.steps),
		.calibration_scale_numerator = 1,
		.calibration_offset_numerator = -zero_level * 32,
		.calibration_denominator = 32,
		.creation_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(
			time_from_str(header.start_time)),
		.device_model = std::string{model},
		.writer_application = "Spectrum Saver splogconvert",
		.device_identifier = {},
		.user_comment = "Converted from the legacy text log format"
	};
}

[[nodiscard]] std::uint16_t reconstruct_raw(
	const float power,
	const LogFileMetadata &metadata
)
{
	const auto raw = (static_cast<double>(power) * metadata.calibration_denominator
		- metadata.calibration_offset_numerator) / metadata.calibration_scale_numerator;
	throw_if(!std::isfinite(raw) || raw < 0
		|| raw > std::numeric_limits<std::uint16_t>::max(),
		"Calibrated sample cannot be represented by RAW_U16_AFFINE");
	return static_cast<std::uint16_t>(std::llround(raw));
}

void write_binary(
	std::ostream &output,
	LogFileMetadata metadata,
	const std::span<const LogHeader> headers,
	const std::span<const float> power_data
)
{
	throw_if(headers.empty(), "Cannot write an empty binary log");
	throw_if(headers.size() > std::numeric_limits<std::size_t>::max() / metadata.points_per_sweep
		|| power_data.size() != headers.size() * metadata.points_per_sweep,
		"Power sample count does not match the record layout");
	BinaryLogWriter writer{output, metadata};
	for(std::size_t record_index = 0; record_index < headers.size(); ++record_index)
	{
		std::vector<std::uint16_t> samples;
		samples.reserve(headers[record_index].steps);
		const auto start = record_index * headers[record_index].steps;
		for(const auto power : power_data.subspan(start, headers[record_index].steps))
			samples.emplace_back(reconstruct_raw(power, metadata));
		writer.write(SweepRecord{
			.sequence = record_index,
			.start_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(
				time_from_str(headers[record_index].start_time)),
			.end_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(
				time_from_str(headers[record_index].end_time)),
			.samples = std::move(samples)
		});
	}
	writer.close();
}

void write_text(
	std::ostream &output,
	const std::span<const LogHeader> headers,
	const std::span<const float> power_data
)
{
	std::size_t sample_offset = 0;
	for(const auto &header : headers)
	{
		throw_if(header.steps > power_data.size() - sample_offset,
			"Power sample count does not match the text record layout");
		output << std::format("$ {:.06f},{:.06f},{},{:.03f},{},{}\n",
			header.start_freq, header.stop_freq, header.steps, header.rbw,
			header.start_time, header.end_time);
		for(const auto power : power_data.subspan(sample_offset, header.steps))
			output << std::format("{:.1f}\n", power);
		output << '\n';
		sample_offset += header.steps;
	}
	throw_if(sample_offset != power_data.size(), "Unconsumed power samples remain after conversion");
	throw_if(!output, "Failed to write converted text log");
}
}

int main(int argc, char *argv[])
try
{
	const auto options = parse_arguments(argc, argv);
	std::ifstream input_file;
	std::istream *input = &std::cin;
	if(options.input_name != "-")
	{
		input_file.open(options.input_name, std::ios::in | std::ios::binary);
		throw_if(!input_file.is_open(), std::format("Cannot open input file: {}", options.input_name));
		input = &input_file;
	}
	std::vector<float> power_data;
	std::vector<LogHeader> headers;
	const auto result = read_logfile(*input, options.input_format, power_data, headers);
	if(result.truncated_tail)
		std::cerr << "Warning: ignored an incomplete final binary record\n";
	throw_if(result.format == options.output_format,
		"Input and output log formats are already the same");

	std::ofstream output_file;
	std::ostream *output = &std::cout;
	if(options.output_name != "-")
	{
		output_file.open(options.output_name, std::ios::out | std::ios::binary);
		throw_if(!output_file.is_open(), std::format("Cannot open output file: {}", options.output_name));
		output = &output_file;
	}

	if(options.output_format == LogFormat::text)
		write_text(*output, headers, power_data);
	else
	{
		throw_if(!options.model,
			"Converting a text log to binary requires --model tinySA|tinySA4");
		auto metadata = metadata_from_text(headers, *options.model);
		std::cerr << "Warning: legacy decimal samples are mapped to the nearest device raw value\n";
		write_binary(*output, std::move(metadata), headers, power_data);
	}
	output->flush();
	throw_if(!*output, "Failed to flush converted log");
	std::cerr << std::format("Converted {} {} record(s) from {} to {}\n",
		options.input_name, headers.size(), log_format_name(result.format),
		log_format_name(options.output_format));
	return EXIT_SUCCESS;
}
catch(const std::exception &error)
{
	std::cerr << error.what() << '\n';
	return EXIT_FAILURE;
}
