/*
 *   log2png - convert a log file to spectrogram
 *   Copyright (C) 2023 Kelei Chen
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "common.hpp"
#include "config.hpp"
#include <Magick++.h>
#include <tinycolormap.hpp>

using namespace Magick;
using MagickCore::Quantum;

void draw_spectrogram(
	const size_t sp_width,
	const size_t sp_height,
	const size_t sp_xoffset,
	const size_t sp_yoffset,
	vector<float> &power_data,
	Image &image
)
{
	Quantum *pixels = image.getPixels(sp_xoffset, sp_yoffset, sp_width, sp_height);
	const int channels = image.channels();

	// Measure speed
	auto drawing_start_time = now();

	// trivial to parallelize, so why not?
	#pragma omp parallel for
	for(size_t i = 0; i < power_data.size(); i++)
	{
		const double value = (power_data.at(i) + 120) / 100;
		const auto mappedcolor = tinycolormap::GetColor(value, tinycolormap::ColormapType::Cubehelix);

		// Raw pixel access is faster than directly using pixelColor()
		pixels[i * channels + 0] = QuantumRange * mappedcolor.r();
		pixels[i * channels + 1] = QuantumRange * mappedcolor.g();
		pixels[i * channels + 2] = QuantumRange * mappedcolor.b();
	}
	image.syncPixels();

	const auto drawing_end_time = now();
	const auto drawing_duration = duration_cast<std::chrono::nanoseconds>(drawing_end_time - drawing_start_time);
	assert(drawing_duration.count() > 0);
	const size_t spectrogram_pixel_count = power_data.size();

	print("Drawn spectrogram: {:.6f}Mpix took {:.3f} seconds, at {:.3f}Mpix/s\n",
		(double)spectrogram_pixel_count / 1e6, // Mpix
		(double)drawing_duration.count() / 1e9, // seconds
		(double)spectrogram_pixel_count * 1e3 / (drawing_duration.count()) // Mpix/s
	);

}

void draw_text
(
	const string &text,
	const int px,
	const Magick::Color &color,
	const Magick::Geometry &geom,
	const Magick::GravityType &gravity,
	Image &image
)
{
	image.fontPointsize(PX_TO_PT(px));
	image.fillColor(color);
	image.annotate(text, geom, gravity);
	image.modifyImage();
}

void draw_vertical_gridlines(const size_t steps, const size_t records, const logheader_t &h, Image &image)
{
	const size_t xoffset = 0;
	const size_t yoffset = BANNER_HEIGHT;

	// draw vertical gridlines
	// calculate gridline spacing from frequency range

	const size_t start_freq = h.start_freq * 1e6;
	const size_t stop_freq = h.stop_freq * 1e6;
	const size_t step_freq = (stop_freq - start_freq) / (steps - 1);
	const size_t freq_range = stop_freq - start_freq; // convert to Hz for easier calculation
	size_t gridline_exponent = 100ULL * 1000 * 1000 * 1000; // 100 GHz
	size_t gridline_spacing = SIZE_MAX;

	Color gridline_color("grey");
	gridline_color.quantumAlpha(QuantumRange * 0.75);
	image.strokeColor(gridline_color);
	image.strokeWidth(1);
	image.strokeAntiAlias(false);

	// find a gridline spacing that will result in at least MIN_GRIDLINES gridlines
	while(freq_range / gridline_spacing < MIN_GRIDLINES)
	{
		gridline_spacing = gridline_exponent * 5;
		if(freq_range / gridline_spacing >= MIN_GRIDLINES)
			break;
		gridline_spacing = gridline_exponent * 2;
		if(freq_range / gridline_spacing >= MIN_GRIDLINES)
			break;
		gridline_spacing = gridline_exponent;
		gridline_exponent /= 10;
	}

	print("Drawing frequency grid, freq_range: {} Hz, gridline_spacing: {} Hz\n", freq_range, gridline_spacing);

	const size_t gridline_count = freq_range / gridline_spacing + 1;
	// find point of the last gridline
	const size_t last_gridline_point =  ((stop_freq / gridline_spacing * gridline_spacing) - start_freq) / step_freq;

	std::vector<Magick::Drawable> draw_list;
	for(size_t i = 0; i < gridline_count; i++)
	{
		const size_t x = xoffset + last_gridline_point - i * (gridline_spacing / step_freq);
		const size_t h = records;
		draw_list.emplace_back(Magick::DrawableLine(x, yoffset, x, yoffset + h - 1));
	}
	image.draw(draw_list);
	image.modifyImage();
}

static fstream logfile_stream;

static string logfile_name = "";
static string filename_prefix = "sp";
static string graph_title = "Unnamed Spectrogram";
static bool do_gridlines = true;

bool parse_args(int argc, char *argv[])
{
	int opt;

	while((opt = getopt(argc, argv, "f:p:t:g:h")) != -1)
	{
		switch(opt)
		{
			case 'f':
				logfile_name = optarg;
				break;
			case 'p':
				filename_prefix = optarg;
				break;
			case 't':
				graph_title = optarg;
				break;
			case 'g':
				if(string(optarg) == "true")
					do_gridlines = true;
				else if(string(optarg) == "false")
					do_gridlines = false;
				else
				{
					cerr << "Error: invalid value for -g: " << optarg << endl;
					return false;
				}
				break;
			case 'h':
			default:
				cerr << "Usage: " << argv[0] <<
					" [-f <log file>] [-p <filename prefix>] [-t <graph title>] [-g <grid? true/false>]" << endl;
				return false;
		}
	}

	if_error(logfile_name.empty(), "Error: no log file specified (-f).");

	return true;
}

int main(int argc, char *argv[])
{
try
{
	
	Magick::InitializeMagick(*argv);

	if(parse_args(argc, argv) == false)
		return EXIT_FAILURE;

/* ==================== *\
|| Text Processing Part ||
\* ==================== */

	vector<logheader_t> headers;
	vector<float> power_data;

	// open log file
	// go through all headers to get record count & validate everything
	if(logfile_name == "-")
	{
		parse_logfile(power_data, headers, cin);
	}
	else
	{
		logfile_stream.open(logfile_name, ios::in);
		if_error(!logfile_stream.is_open(), "Error: could not open file " + logfile_name);

		parse_logfile(power_data, headers, logfile_stream);
	}

	logproblem_t problems = {};
	check_logfile_time_consistency(headers, problems);

	const auto record_count = headers.size();
	// get last header for easy access
	const auto &h = headers.back();

	print("{} has {} records, {} points each\n", logfile_name, record_count, h.steps);

/* ===================== *\
|| Image Processing Part ||
\* ===================== */

	// ex. sp.20230320T220505.png
	string output_name = filename_prefix + "." + h.end_time + ".png";

	// create the image

	const size_t sp_width = h.steps;
	const size_t sp_height = record_count;
	const size_t sp_xoffset = 0;	// Currently unused
	const size_t sp_yoffset = BANNER_HEIGHT;

	const size_t width = h.steps;
	const size_t height = record_count + BANNER_HEIGHT + FOOTER_HEIGHT;

	Image image(Geometry(width, height), Color("black"));
	image.verbose(true);
	image.type(TrueColorType);
	image.depth(8); // 8 bits per channel is enough for most usage
	image.textAntiAlias(true);
	image.fontFamily(FONT_FAMILY);
	image.comment(graph_title);
	image.modifyImage();

	// Write banner text
	draw_text(graph_title, BANNER_HEIGHT, BANNER_COLOR, Geometry(0, 0, 0, 0), Magick::NorthWestGravity, image);

	draw_spectrogram(sp_width, sp_height, sp_xoffset, sp_yoffset, power_data, image);

	const string current_time = time_str();

	// Footer text
	const string footer_info = format("Start: {}, Stop: {}, From {:.6f}MHz to {:.6f}MHz, {} Records, {} Steps, RBW: {:.1f}kHz, Generated on {}",
		headers.front().start_time, h.end_time, h.start_freq, h.stop_freq, record_count, h.steps, h.rbw, current_time);
	draw_text(footer_info, FOOTER_HEIGHT, FOOTER_COLOR, Geometry(0, 0, 0, 0), Magick::SouthEastGravity, image);

	// Draw gridlines
	if(do_gridlines)
	{
		draw_vertical_gridlines(h.steps, record_count, h, image);
	}

	// write the image to a file
	print("[{}] Writing image: ", current_time);
	// Enabled verbose
	image.write(output_name);
}
catch(const StringException &e)
{
	cerr << e.what() << endl;
	return EXIT_FAILURE;
}

	return EXIT_SUCCESS;
}
