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

// parse log record header line
// # <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
// formatted by:
//	"# %.06f,%.06f,%ld,%.03f,%s,%s\n"
bool parse_header(const string &line, logheader_t &h)
{
	char start_time_str[32];
	char end_time_str[32];
	if(line[0] != '$')
		return false;
	
	int ret = sscanf(line.c_str(), "$ %lf,%lf,%zu,%f,%31[^,],%31[^,]", &h.start_freq, &h.stop_freq, &h.steps, &h.rbw, start_time_str, end_time_str);
	h.start_time = start_time_str;
	h.end_time = end_time_str;

	if(ret != 6)
		return false;

	// sanity check
	if(h.start_freq >= h.stop_freq)
	{
		cerr << "Error: start_freq >= stop_freq" << endl;
		return false;
	}
	if(h.steps == 0)
	{
		cerr << "Error: steps == 0" << endl;
		return false;
	}
	if(h.rbw <= 0 || h.rbw > 1000)
	{
		cerr << "Error: rbw <= 0 || rbw > 1000" << endl;
		return false;
	}

	return true;
}

// parse log file
void parse_logfile(
	vector<float> &power_data,
	vector<logheader_t> &headers,
	istream &logfile_stream
)
{
	logheader_t h; // current header
	logheader_t first_header
	{
		.start_freq = 0,
		.stop_freq = 0,
		.steps = 0,
		.rbw = 0,
		.start_time = "",
		.end_time = ""
	};

	string line;
	size_t line_count = 0;
	size_t lines_per_record = SIZE_MAX;

	// types of lines:
	// 	record header: # <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
	// 	data: <dbm>\n<dbm>\n<dbm>\n...
	// 	trailing newline of a record: \n
	// any other line is invalid

	while(getline(logfile_stream, line))
	{
		if(line[0] == '#')
			continue; // comment line

		line_count++;

		// parse header
		if(line_count % lines_per_record == 1)
		{
			bool ret = parse_header(line, h);
			if_error(!ret, format("Error: invalid header at line #{}", line_count));

			if(first_header.steps == 0)
			{
				lines_per_record = h.steps + 2; // +1 for header, +1 for trailing newline
				first_header = h;
			}
			else
			{
				if_error(h.start_freq != first_header.start_freq,
					format("Error: start_freq mismatch at line #{}: {} != {}",
						line_count, h.start_freq, first_header.start_freq));
				if_error(h.stop_freq != first_header.stop_freq,
					format("Error: stop_freq mismatch at line #{}: {} != {}",
						line_count, h.stop_freq, first_header.stop_freq));
				if_error(h.steps != first_header.steps,
					format("Error: steps count mismatch at line #{}: {} != {}",
						line_count, h.steps, first_header.steps));
				if_error(h.rbw != first_header.rbw,
					format("Error: rbw mismatch at line #{}: {} != {}",
						line_count, h.rbw, first_header.rbw));
			}

			headers.emplace_back(h);
			continue;
		}
		else if(line_count % lines_per_record == 0)
		{
			// trailing newline of a record
			if_error(!line.empty(), format("Error: newline expected at line #{}", line_count));
			continue;
		}
		else
		{
			// data line
			try
			{
				power_data.emplace_back(std::stod(line));
			}
			catch(const std::exception& e)
			{
				cerr << format("std::stod exception: {}\n", e.what());
				cerr << format("Error: failed to parse double from line {}: \"{}\"\n", line_count, line);
			}
			continue;
		}
	}

	if_error(headers.size() == 0, "Error: no valid record found in log file");

	// check if size of power_data is correct
	if(power_data.size() != headers.size() * first_header.steps)
		if_error(true, "Error: power_data count is not correct");
}

void draw_spectrogram(const size_t width, const size_t height, vector<float> &power_data, Image &image)
{
	Pixels view(image);
	Quantum *pixels = view.get(0, 0, width, height);
	const uint8_t channels = image.channels();

	pixels += width * channels * (BANNER_HEIGHT); // skip banner

	// Measure speed
	auto drawing_start_time = now();

	// trivial to parallelize, so why not?
	#pragma omp parallel for
	for(size_t i = 0; i < power_data.size(); i++)
	{
		const double value = (power_data.at(i) + 120) / 100;
		const auto mappedcolor = tinycolormap::GetColor(value, tinycolormap::ColormapType::Cubehelix);

		// Raw pixel access is faster than directly using pixelColor()
		pixels[i * channels + 0] = mappedcolor.r() * QuantumRange;
		pixels[i * channels + 1] = mappedcolor.g() * QuantumRange;
		pixels[i * channels + 2] = mappedcolor.b() * QuantumRange;
	}
	view.sync();
	image.modifyImage();

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

	check_logfile_time_consistency(headers);

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

	draw_spectrogram(width, height, power_data, image);

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
