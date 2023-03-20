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

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <unistd.h>
#include <limits.h>
#include <omp.h>
#include <Magick++.h>
#include <tinycolormap.hpp>
#include "common.hpp"
#include "config.hpp"

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::to_string;
using std::vector;
using std::fstream;
using namespace Magick;
using MagickCore::Quantum;
Quantum MaxRGB = QuantumRange;

static const std::chrono::time_point<std::chrono::system_clock> time_from_str(const string &str)
{
	std::istringstream ss(str);
	std::tm tm;
	ss >> std::get_time(&tm, "%Y%m%dT%H%M%S");
	if_error(!ss.fail(), "Failed to parse time string");
	return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// parse log record header line
// # <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
// formatted by:
//	"# %.06f,%.06f,%ld,%.03f,%s,%s\n"
bool parse_header(const string &line, log_header_t &h)
{
	char start_time_str[32];
	char end_time_str[32];
	if(line[0] != '#')
		return false;
	
	sscanf(line.c_str(), "# %lf,%lf,%zu,%f,%31[^,],%31[^,]", &h.start_freq, &h.stop_freq, &h.steps, &h.rbw, start_time_str, end_time_str);
	h.start_time = start_time_str;
	h.end_time = end_time_str;

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
	log_header_t &last_header,
	size_t &record_count,
	fstream &logfile_stream,
	string &first_start_time)
{
	log_header_t h; // current header
	log_header_t first_header
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
		line_count++;

		// parse header
		if(line_count % lines_per_record == 1)
		{
			bool ret = parse_header(line, h);
			if(!ret)
				if_error(true, "Error: invalid header line");

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
			record_count++;
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

	if_error(record_count == 0, "Error: no valid record found in log file");

	// check if size of power_data is correct
	if(power_data.size() != record_count * first_header.steps)
		if_error(true, "Error: power_data count is not correct");

	// save the first start time
	first_start_time = first_header.start_time;
	// store the last header
	last_header = h;
}

void draw_spectrogram(size_t width, vector<float> &power_data, Quantum *pixels)
{
	// trivial to parallelize, so why not?
	#pragma omp parallel for
	for(size_t i = 0; i < power_data.size(); i++)
	{
		// get x & y coordinates
		const size_t x = i % width;
		const size_t y = i / width;

		const auto mappedcolor = tinycolormap::GetColor((power_data.at(i) + 120) / 100, tinycolormap::ColormapType::Cubehelix);

		// Raw pixel access is faster than directly using pixelColor()
		pixels[(y + BANNER_HEIGHT) * width * 4 + x * 4 + 0] = mappedcolor.r() * MaxRGB;
		pixels[(y + BANNER_HEIGHT) * width * 4 + x * 4 + 1] = mappedcolor.g() * MaxRGB;
		pixels[(y + BANNER_HEIGHT) * width * 4 + x * 4 + 2] = mappedcolor.b() * MaxRGB;
		// ignore alpha channel
	}
}

int main(int argc, char *argv[])
{
	fstream logfile_stream;

	string logfile_name = "";
	string filename_prefix = "sp";
	string graph_title = "Unnamed Spectrogram";

	int opt;

	while((opt = getopt(argc, argv, "f:p:t:h")) != -1)
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
			case 'h':
			default:
				cerr << "Usage: " << argv[0] << " [-f <log file>] [-p <filename prefix>] [-t <graph title>]" << endl;
				return 1;
		}
	}

	if_error(logfile_name.empty(), "Error: no log file specified (-f).");

	// file info
	size_t record_count = 0;

	Magick::InitializeMagick(*argv);

	// open log file
	logfile_stream.open(logfile_name, std::ios::in);
	if_error(!logfile_stream.is_open(), "Error: could not open file " + logfile_name);

	log_header_t h;
	vector<float> power_data;
	string first_start_time = "";

	// go through all headers to get record count & validate everything
	parse_logfile(power_data, h, record_count, logfile_stream, first_start_time);

	print("{} has {} records, {} points each\n", logfile_name, record_count, h.steps);
	
	// remove records if total number exceeds MAX_RECORDS
	if(record_count > MAX_RECORDS)
	{
		print("Warning: total number of records exceeds {}, removing {} records from the beginning\n",
			MAX_RECORDS, record_count - MAX_RECORDS);
		power_data.erase(power_data.begin(), power_data.begin() + (record_count - MAX_RECORDS) * h.steps);
		record_count = MAX_RECORDS;
	}

	string output_name = filename_prefix + "." + h.end_time + ".png";

	// create the image

	const size_t width = h.steps;
	const size_t height = record_count + BANNER_HEIGHT + FOOTER_HEIGHT;

	Image image(Geometry(width, height), Color("black"));
	image.type(TrueColorType);
	image.comment(graph_title);

	// Set font style & color
	image.textAntiAlias(true);
	image.fontFamily(FONT_FAMILY);
	image.fillColor(Color(BANNER_COLOR));
	// Write banner text
	image.fontPointsize(PX_TO_PT(BANNER_HEIGHT));
	image.annotate(graph_title, Magick::Geometry(0, 0, 0, 0), Magick::NorthWestGravity);
	image.modifyImage();

	Pixels view(image);
	Quantum *pixels = view.get(0, 0, width, height);

	auto drawing_start_time = std::chrono::system_clock::now();

	print("Drawing spectrogram... ");
	draw_spectrogram(width, power_data, pixels);
	view.sync();
	image.modifyImage();

	auto drawing_end_time = std::chrono::system_clock::now();
	auto drawing_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(drawing_end_time - drawing_start_time);
	assert(drawing_duration.count() > 0);
	size_t spectrogram_pixel_count = h.steps * record_count;

	print("drawing {:.6f}Mpix took {:.3f} seconds, at {:.3f}Mpix/s\n",
		(double)spectrogram_pixel_count / 1e6, // Mpix
		(double)drawing_duration.count() / 1e9, // seconds
		(double)spectrogram_pixel_count * 1e3 / (drawing_duration.count()) // Mpix/s
	);

	const string current_time = time_str();

	// Footer text
	image.fontPointsize(PX_TO_PT(FOOTER_HEIGHT));
	image.fillColor(Color(FOOTER_COLOR));
	const string footer_info = format("Start: {}, Stop: {}, From {:.6f}MHz to {:.6f}MHz, {} Records, {} Steps, RBW: {:.1f}kHz, Generated on {}",
		first_start_time, h.end_time, h.start_freq, h.stop_freq, record_count, h.steps, h.rbw, current_time);
	image.annotate(footer_info, Magick::Geometry(0, 0, 0, 0), Magick::SouthEastGravity);
	image.modifyImage();

	// write the image to a file
	print("[{}] Writing image to {} ({}x{})\n", current_time, output_name, width, height);
	image.write(output_name);

	return 0;
}
