/*
 *   log2png - convert a directory of log files to a spectrogram
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
using std::vector;
using std::sort;
using std::fstream;
using std::filesystem::directory_iterator;
using std::filesystem::path;
using namespace Magick;
using MagickCore::Quantum;
Quantum MaxRGB = QuantumRange;

int main(int argc, char *argv[])
{

	if(argc < 4)
	{
		cout << "Usage: log2png <dir> <filename prefix> <graph title>" << endl;
		return 1;
	}
	string dir = argv[1];
	string filename_prefix = argv[2];
	string graph_title = argv[3];

	InitializeMagick(*argv);

	// Get all CSV files in the current directory
	vector<string> log_files;
	test_error(!std::filesystem::is_directory(dir), "Invalid directory: " + dir);
	for (const auto & entry : directory_iterator(path(dir)))
	{
		if (entry.path().extension() == ".log")
		{
			log_files.push_back(entry.path().filename());
		}
	}

	// Sort the files by name
	sort(log_files.begin(), log_files.end());

	vector<size_t> line_counts;
	// count the number of lines in the files
	for(auto & file : log_files)
	{
		std::ifstream in_file(dir + "/" + file);
		size_t line_count = 0;
		string line;
		while (std::getline(in_file, line))
		{
			line_count++;
		}
		line_counts.push_back(line_count);
	}

	// check if all line counts are equal
	size_t line_count = line_counts[0]; // will be used later
	for(auto & count : line_counts)
	{
		test_error(count != line_count, "Line count mismatch: " + std::to_string(count) + " != " + std::to_string(line_count));
	}
	line_counts.clear(); // free memory

	line_count -= 1; // subtract the header line

	size_t file_count = log_files.size(); // this will determine the height of the spectrogram
	cout << "Found " << file_count << " files with " << line_count << " measurements each" << endl;

	if(file_count > MAX_ROWS)
	{
		log_files.erase(log_files.begin(), log_files.begin() + file_count - MAX_ROWS);
		cerr << "Warning: too many files, only the last " << MAX_ROWS << " files will be used" << endl;
		file_count = MAX_ROWS;
	}

	// Get the date time from the last file name
	string last_log_time_str = log_files.back();
	size_t pos = last_log_time_str.find_last_of(".");
	last_log_time_str.erase(pos, last_log_time_str.length() - pos); // remove the file extension
	pos = last_log_time_str.find_last_of("."); // find the last dot, which separates name prefix and date time
	last_log_time_str.erase(0, pos + 1); // remove the name prefix

	string output_name = filename_prefix + "." + last_log_time_str + ".png";

	// create the image

	size_t width = line_count;
	size_t height = file_count + BANNER_HEIGHT + FOOTER_HEIGHT;

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
	// read the data from the files & draw the image
	for(size_t i = 0; i < log_files.size(); i++)
	{
		// open the file
		fstream log_file;
		log_file.open(dir + "/" + log_files[i], std::ios::in);
		if(!log_file.is_open())
		{
			cout << "Error: could not open file " << log_files[i] << endl;
			return 1;
		}

		for(size_t x = 0; x < line_count; x++)
		{
			string line;
			getline(log_file, line);
			if(x == 0) // skip the header line
			{
				continue;
			}

			// convert the power in dBm (-120dBm to 0dBm) to a color
			// display range is -20 to -120 dBm (reduced dynamic range for better visibility)
			double freq = 0;
			double power_dBm = 0;
			sscanf(line.c_str(), "%lf,%lf", &freq, &power_dBm);

			const auto mappedcolor = tinycolormap::GetColor((power_dBm + 120) / 100, tinycolormap::ColormapType::Cubehelix);

			// Raw pixel access is faster than directly using pixelColor()
			pixels[(i + BANNER_HEIGHT) * width * 4 + x * 4 + 0] = mappedcolor.r() * MaxRGB;
			pixels[(i + BANNER_HEIGHT) * width * 4 + x * 4 + 1] = mappedcolor.g() * MaxRGB;
			pixels[(i + BANNER_HEIGHT) * width * 4 + x * 4 + 2] = mappedcolor.b() * MaxRGB;
			// ignore alpha channel
			view.sync();
		}

		log_file.close();
	}

	image.modifyImage();

	auto drawing_end_time = std::chrono::system_clock::now();
	auto drawing_duration = std::chrono::duration_cast<std::chrono::microseconds>(drawing_end_time - drawing_start_time);
	assert(drawing_duration.count() > 0);
	size_t spectrogram_pixel_count = line_count * file_count;

	cerr << "Drawing took " << (double)drawing_duration.count() / 1e6 << " seconds, at " <<
		(double)spectrogram_pixel_count / drawing_duration.count() << "Mpix/s" << endl;

	string current_time = time_str();

	// Footer text
	image.fontPointsize(24); // about 32px
	image.annotate("Latest Sweep: " + last_log_time_str + ", Generated on " + current_time
		,Magick::Geometry(0, 0, 0, 0), Magick::SouthEastGravity);
	image.modifyImage();

	// write the image to a file
	cerr << "[" << current_time <<  "] Writing image to " << output_name << " (" << width << 'x' << height << ")" << endl;
	image.write(output_name);

	return 0;
}