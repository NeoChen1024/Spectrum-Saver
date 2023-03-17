/*
 *   log2png - convert a log files to spectrogram
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
#include "common.hpp"
#include "config.hpp"
#include "tinycolormap/include/tinycolormap.hpp"

#define BANNER_HEIGHT 64 // 48pt
#define FOOTER_HEIGHT 32 // 24pt

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

	if(argc < 4)
	{
		cout << "Usage: log2png <logfile> <filename prefix> <graph title>" << endl;
		return 1;
	}
	string logfile = argv[1];
	string filename_prefix = argv[2];
	string graph_title = argv[3];

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
	test_error(!log_file.is_open(), "Error: could not open file " + logfile);

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
				if(i == steps && line.empty())
					continue; // last line is empty, skip it
				try
				{
					power_data.emplace_back(std::stof(line));
				}
				catch(const std::exception& e)
				{
					std::cerr << e.what() << '\n';
					test_error(true, "Error: at record #" + to_string(record_count) + ", invalid data line: " + line);
				}
			}
		}
		else
		{
			test_error(true, "Error: invalid header line: " + line);
		}
	}

	// check if all records have the same number of steps
	for(size_t i = 1; i < record_count; i++)
	{
		if(step_counts[i] != step_counts[i - 1])
			test_error(true, "Error: record #" + to_string(i) + " has different number of steps than record #1");
	}

	// check if size of power_data is correct
	if(power_data.size() != record_count * steps)
		test_error(true, "Error: power_data count is not correct");
	
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
	image.fillColor(Color("white"));
	// Write banner text
	image.fontPointsize(48); // about 64px
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
	auto drawing_duration = std::chrono::duration_cast<std::chrono::microseconds>(drawing_end_time - drawing_start_time);
	assert(drawing_duration.count() > 0);
	size_t spectrogram_pixel_count = steps * record_count;

	cerr << "Drawing took " << (double)drawing_duration.count() / 1e6 << " seconds, at " <<
		(double)spectrogram_pixel_count / drawing_duration.count() << "Mpix/s" << endl;

	string current_time = time_str();

	// Footer text
	image.fontPointsize(24); // about 32px
	image.annotate("Latest Sweep: " + end_time + ", Generated on " + current_time
		,Magick::Geometry(0, 0, 0, 0), Magick::SouthEastGravity);
	image.modifyImage();

	// write the image to a file
	cerr << "[" << current_time <<  "] Writing image to " << output_name << " (" << width << 'x' << height << ")" << endl;
	image.write(output_name);

	return 0;
}