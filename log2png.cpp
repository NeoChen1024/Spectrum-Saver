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
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <Magick++.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <unistd.h>
#include <limits.h>
#include "common.hpp"
#include "config.hpp"
#include "tinycolormap/include/tinycolormap.hpp"

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::string_view;
using std::to_string;
using std::vector;
using std::sort;
using std::fstream;
using std::filesystem::directory_iterator;
using std::filesystem::path;
using namespace Magick;
using MagickCore::Quantum;
Quantum MaxRGB = QuantumRange;

// parse log record header line
// # <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
// formatted by:
//	"# %.06f,%.06f,%ld,%.03f,%s,%s\n"
bool parse_header(const string &line, double &start_freq, double &stop_freq, size_t &steps, float &rbw, string &start_time, string &end_time)
{
	char start_time_str[32];
	char end_time_str[32];
	if(line[0] != '#')
		return false;
	
	sscanf(line.c_str(), "# %lf,%lf,%zu,%f,%[^,],%[^,]", &start_freq, &stop_freq, &steps, &rbw, start_time_str, end_time_str);
	start_time = start_time_str;
	end_time = end_time_str;

	// sanity check
	if(start_freq >= stop_freq)
		return false;
	if(steps == 0)
		return false;
	if(rbw <= 0 || rbw > 1000)
		return false;

	return true;
}

int main(int argc, char *argv[])
{
	fstream log_file;

	string logfile = "";
	string filename_prefix = "sp";
	string graph_title = "Unnamed Spectrogram";

	int opt;

	while((opt = getopt(argc, argv, "f:p:t:h")) != -1)
	{
		switch(opt)
		{
			case 'f':
				logfile = optarg;
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

	// header data
	double start_freq = 0;
	double stop_freq = 0;
	size_t steps = 0;
	float rbw = 0;
	string start_time;
	string end_time;

	// file info
	size_t record_count = 0;

	InitializeMagick(*argv);

	// open log file
	log_file.open(logfile, std::ios::in);
	if_error(!log_file.is_open(), "Error: could not open file " + logfile);

	vector<float> power_data;
	vector<size_t> step_counts;

	// go through all headers to get record count & validate everything
	string line;
	while(getline(log_file, line))
	{
		if(parse_header(line, start_freq, stop_freq, steps, rbw, start_time, end_time))
		{
			step_counts.emplace_back(steps);
			record_count++;
			for(size_t i = 0; i < steps + 1; i++)
			{
				getline(log_file, line);
				// check if it's valid floating point number
				if(i == steps)
				{
					if_error(line != "", "Error: at record #" + to_string(record_count) + ", last line of record is not empty");
					continue; // last line is empty, skip it
				}

				try
				{
					power_data.emplace_back(std::stof(line));
				}
				catch(const std::exception& e)
				{
					std::cerr << e.what() << '\n';
					if_error(true, "Error: at record #" + to_string(record_count) + ", invalid data line: " + line);
				}
			}
		}
		else
		{
			if_error(true, "Error: invalid header line: " + line);
		}
	}

	// check if all records have the same number of steps
	for(size_t i = 1; i < record_count; i++)
	{
		if(step_counts[i] != step_counts[i - 1])
			if_error(true, "Error: record #" + to_string(i) + " has different number of steps than record #1");
	}

	// check if size of power_data is correct
	if(power_data.size() != record_count * steps)
		if_error(true, "Error: power_data count is not correct");

	cerr << logfile << " has " << record_count << " records, " << steps << " points each" << endl;
	
	// remove records if total number exceeds MAX_RECORDS
	if(record_count > MAX_RECORDS)
	{
		cout << "Warning: total number of records exceeds " << MAX_RECORDS << ", removing records from the beginning" << endl;
		power_data.erase(power_data.begin(), power_data.begin() + (record_count - MAX_RECORDS) * steps);
		record_count = MAX_RECORDS;
	}

	const string last_end_time = end_time;

	string output_name = filename_prefix + "." + last_end_time + ".png";

	// create the image

	const size_t width = steps;
	const size_t height = record_count + BANNER_HEIGHT + FOOTER_HEIGHT;

	Image image(Geometry(width, height), Color("black"));
	image.type(TrueColorType);

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

	cerr << "Drawing spectrogram..." << endl;
	for(size_t i = 0; i < power_data.size(); i++)
	{
		// get x & y coordinates
		const size_t x = i % steps;
		const size_t y = i / steps;

		const auto mappedcolor = tinycolormap::GetColor((power_data.at(i) + 120) / 100, tinycolormap::ColormapType::Cubehelix);

		// Raw pixel access is faster than directly using pixelColor()
		pixels[(y + BANNER_HEIGHT) * width * 4 + x * 4 + 0] = mappedcolor.r() * MaxRGB;
		pixels[(y + BANNER_HEIGHT) * width * 4 + x * 4 + 1] = mappedcolor.g() * MaxRGB;
		pixels[(y + BANNER_HEIGHT) * width * 4 + x * 4 + 2] = mappedcolor.b() * MaxRGB;
		// ignore alpha channel
	}
	view.sync();
	image.modifyImage();

	auto drawing_end_time = std::chrono::system_clock::now();
	auto drawing_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(drawing_end_time - drawing_start_time);
	assert(drawing_duration.count() > 0);
	size_t spectrogram_pixel_count = steps * record_count;

	cerr << "Drawing took " << (double)drawing_duration.count() / 1e9 << " seconds, at " <<
		(double)spectrogram_pixel_count / (drawing_duration.count() / 1e3) << "Mpix/s" << endl;

	string current_time = time_str();

	// Footer text
	char footer_info[PATH_MAX];
	image.fontPointsize(PX_TO_PT(FOOTER_HEIGHT));
	image.fillColor(Color(FOOTER_COLOR));
	sprintf(footer_info, "Start: %s, Stop: %s, From %.06lfMHz to %.06lfMHz, RBW: %.01fkHz",
		start_time.c_str(), end_time.c_str(), start_freq, stop_freq, rbw);
	image.annotate(string(footer_info) + ", Generated on " + current_time
		,Magick::Geometry(0, 0, 0, 0), Magick::SouthEastGravity);
	image.modifyImage();

	// write the image to a file
	cerr << "[" << current_time <<  "] Writing image to " << output_name << " (" << width << 'x' << height << ")" << endl;
	image.write(output_name);

	return 0;
}
