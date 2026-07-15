/*
 *   log2png - convert a log file to spectrogram
 *   Copyright (C) 2023 Kelei Chen
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#include "common.hpp"
#include "config.hpp"

#include <Magick++.h>
#include <tinycolormap.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
using MagickCore::Quantum;

[[nodiscard]] std::size_t checked_pixel_count(
	const std::size_t width,
	const std::size_t height
)
{
	throw_if(width != 0 && height > std::numeric_limits<std::size_t>::max() / width,
		"Pixel view dimensions overflow");
	return width * height;
}

class RgbPixelView
{
public:
	RgbPixelView(
		Magick::Image &image,
		const std::size_t x,
		const std::size_t y,
		const std::size_t width,
		const std::size_t height
	) : pixels_{image}, channels_{image.channels()}, pixel_count_{checked_pixel_count(width, height)}
	{
		throw_if(x > static_cast<std::size_t>(std::numeric_limits<::ssize_t>::max())
			|| y > static_cast<std::size_t>(std::numeric_limits<::ssize_t>::max()),
			"Pixel view offset is outside ImageMagick's coordinate range");
		auto *data = pixels_.get(
			static_cast<::ssize_t>(x), static_cast<::ssize_t>(y), width, height);
		throw_if(data == nullptr, "ImageMagick could not provide a writable pixel view");
		throw_if(channels_ == 0 || pixel_count_ > std::numeric_limits<std::size_t>::max() / channels_,
			"Pixel view buffer size overflow");
		data_ = std::span<Magick::Quantum>{data, pixel_count_ * channels_};

		red_offset_ = channel_offset(Magick::RedPixelChannel);
		green_offset_ = channel_offset(Magick::GreenPixelChannel);
		blue_offset_ = channel_offset(Magick::BluePixelChannel);
	}

	RgbPixelView(const RgbPixelView &) = delete;
	RgbPixelView &operator=(const RgbPixelView &) = delete;

	void set_rgb(
		const std::size_t index,
		const double red,
		const double green,
		const double blue
	) const noexcept
	{
		auto *pixel = data_.data() + index * channels_;
		pixel[red_offset_] = QuantumRange * red;
		pixel[green_offset_] = QuantumRange * green;
		pixel[blue_offset_] = QuantumRange * blue;
	}

	void sync()
	{
		pixels_.sync();
	}

private:
	[[nodiscard]] std::size_t channel_offset(const Magick::PixelChannel channel) const
	{
		const auto offset = pixels_.offset(channel);
		throw_if(offset < 0 || static_cast<std::size_t>(offset) >= channels_,
			"ImageMagick image does not expose the required RGB channels");
		return static_cast<std::size_t>(offset);
	}

	Magick::Pixels pixels_;
	std::span<Magick::Quantum> data_;
	std::size_t channels_;
	std::size_t pixel_count_;
	std::size_t red_offset_{};
	std::size_t green_offset_{};
	std::size_t blue_offset_{};
};

void draw_spectrogram(
	const std::size_t width,
	const std::size_t height,
	const std::size_t x_offset,
	const std::size_t y_offset,
	const std::span<const float> power_data,
	Magick::Image &image
)
{
	throw_if(width != 0 && height > std::numeric_limits<std::size_t>::max() / width,
		"Spectrogram dimensions overflow");
	throw_if(power_data.size() != width * height,
		"Power sample count does not match the spectrogram dimensions");

	RgbPixelView pixels{image, x_offset, y_offset, width, height};
	const auto drawing_start_time = now();

	#pragma omp parallel for
	for(std::size_t index = 0; index < power_data.size(); ++index)
	{
		const double value = (power_data[index] + 120.0) / 100.0;
		const auto color = tinycolormap::GetColor(value, tinycolormap::ColormapType::Cubehelix);
		pixels.set_rgb(index, color.r(), color.g(), color.b());
	}
	pixels.sync();

	const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now() - drawing_start_time);
	throw_if(duration.count() <= 0, "Invalid spectrogram drawing duration");
	print("Drawn spectrogram: {:.6f}Mpix took {:.3f} seconds, at {:.3f}Mpix/s\n",
		static_cast<double>(power_data.size()) / 1e6,
		static_cast<double>(duration.count()) / 1e9,
		static_cast<double>(power_data.size()) * 1e3 / static_cast<double>(duration.count()));
}

void draw_text(
	const std::string_view text,
	const int pixel_size,
	const Magick::Color &color,
	const Magick::Geometry &geometry,
	const Magick::GravityType gravity,
	Magick::Image &image
)
{
	image.fontPointsize(pixels_to_points(pixel_size));
	image.fillColor(color);
	image.annotate(std::string{text}, geometry, gravity);
	image.modifyImage();
}

[[nodiscard]] std::int64_t frequency_to_hz(const double frequency_mhz)
{
	throw_if(!std::isfinite(frequency_mhz) || frequency_mhz < 0
		|| frequency_mhz > static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 1e6,
		"Frequency is outside the supported range");
	return static_cast<std::int64_t>(std::llround(frequency_mhz * 1e6));
}

void draw_vertical_gridlines(
	const std::size_t steps,
	const std::size_t records,
	const LogHeader &header,
	Magick::Image &image
)
{
	throw_if(records == 0 || records > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()),
		"Invalid record count for gridlines");
	const auto start_frequency = frequency_to_hz(header.start_freq);
	const auto stop_frequency = frequency_to_hz(header.stop_freq);
	const auto columns = calculate_gridline_columns(
		start_frequency, stop_frequency, steps, MIN_GRIDLINES);

	Magick::Color gridline_color{"grey"};
	gridline_color.quantumAlpha(QuantumRange * 0.75);
	image.strokeColor(gridline_color);
	image.strokeWidth(1);
	image.strokeAntiAlias(false);

	std::vector<Magick::Drawable> draw_list;
	draw_list.reserve(columns.size());
	const std::int64_t top = BANNER_HEIGHT;
	const std::int64_t bottom = top + static_cast<std::int64_t>(records) - 1;
	for(const auto column : columns)
		draw_list.emplace_back(Magick::DrawableLine(column, top, column, bottom));

	print("Drawing {} frequency gridlines over {} Hz\n",
		columns.size(), stop_frequency - start_frequency);
	image.draw(draw_list);
	image.modifyImage();
}

struct Options
{
	std::string logfile_name;
	std::string filename_prefix{"sp"};
	std::string graph_title{"Unnamed Spectrogram"};
	bool draw_gridlines{true};
};

[[nodiscard]] Options parse_arguments(const int argc, char *argv[])
{
	Options options;
	int argument;
	while((argument = ::getopt(argc, argv, "f:p:t:g:h")) != -1)
	{
		switch(argument)
		{
			case 'f': options.logfile_name = optarg; break;
			case 'p': options.filename_prefix = optarg; break;
			case 't': options.graph_title = optarg; break;
			case 'g':
				if(std::string_view{optarg} == "true")
					options.draw_gridlines = true;
				else if(std::string_view{optarg} == "false")
					options.draw_gridlines = false;
				else
					throw std::runtime_error(std::format("Invalid value for -g: {}", optarg));
				break;
			case 'h':
				std::cout << "Usage: " << argv[0]
					<< " -f <log file> [-p <filename prefix>] [-t <graph title>] "
						"[-g <grid? true/false>]\n";
				std::exit(EXIT_SUCCESS);
			default:
				throw std::runtime_error("Invalid command-line arguments");
		}
	}
	throw_if(options.logfile_name.empty(), "Error: no log file specified (-f)");
	return options;
}
}

int main(int argc, char *argv[])
try
{
	Magick::InitializeMagick(*argv);
	const auto options = parse_arguments(argc, argv);

	std::vector<LogHeader> headers;
	std::vector<float> power_data;
	std::string display_logfile_name = options.logfile_name;
	std::fstream logfile_stream;
	if(options.logfile_name == "-")
	{
		parse_logfile(power_data, headers, std::cin);
		display_logfile_name = "stdin";
	}
	else
	{
		logfile_stream.open(options.logfile_name, std::ios::in);
		throw_if(!logfile_stream.is_open(),
			std::format("Error: could not open file {}", options.logfile_name));
		parse_logfile(power_data, headers, logfile_stream);
	}

	LogProblems problems;
	(void)check_logfile_time_consistency(headers, problems);
	const auto record_count = headers.size();
	const auto &header = headers.back();
	print("{} has {} records, {} points each\n",
		display_logfile_name, record_count, header.steps);

	const auto output_name = std::format(
		"{}.{}.png", options.filename_prefix, header.end_time);
	const std::size_t spectrogram_width = header.steps;
	const std::size_t spectrogram_height = record_count;
	throw_if(record_count > std::numeric_limits<std::size_t>::max()
		- static_cast<std::size_t>(BANNER_HEIGHT + FOOTER_HEIGHT),
		"Image height overflow");
	const std::size_t image_height =
		record_count + static_cast<std::size_t>(BANNER_HEIGHT + FOOTER_HEIGHT);

	Magick::Image image{
		Magick::Geometry{spectrogram_width, image_height}, Magick::Color{"black"}};
	image.verbose(true);
	image.type(Magick::TrueColorType);
	image.depth(8);
	image.textAntiAlias(true);
	image.fontFamily(std::string{FONT_FAMILY});
	image.comment(options.graph_title);
	image.modifyImage();

	draw_text(options.graph_title, BANNER_HEIGHT, Magick::Color{std::string{BANNER_COLOR}},
		Magick::Geometry{}, Magick::NorthWestGravity, image);
	draw_spectrogram(spectrogram_width, spectrogram_height, 0, BANNER_HEIGHT,
		power_data, image);

	const auto current_time = time_str();
	const auto footer = std::format(
		"Start: {}, Stop: {}, From {:.6f}MHz to {:.6f}MHz, {} Records, {} Steps, "
		"RBW: {:.1f}kHz, Generated on {}",
		headers.front().start_time, header.end_time, header.start_freq, header.stop_freq,
		record_count, header.steps, header.rbw, current_time);
	draw_text(footer, FOOTER_HEIGHT, Magick::Color{std::string{FOOTER_COLOR}},
		Magick::Geometry{}, Magick::SouthEastGravity, image);

	if(options.draw_gridlines)
		draw_vertical_gridlines(header.steps, record_count, header, image);

	print("[{}] Writing image: ", current_time);
	image.write(output_name);
	return EXIT_SUCCESS;
}
catch(const std::exception &error)
{
	std::cerr << error.what() << '\n';
	return EXIT_FAILURE;
}
