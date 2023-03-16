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
#include <Magick++.h>
#include <vector>
#include <algorithm>
#include "common.hpp"
#include "config.hpp"
#include "tinycolormap/include/tinycolormap.hpp"

#define BANNER_HEIGHT 64

using std::cout;
using std::cerr;
using std::endl;
using std::string;
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
		if(count != line_count)
		{
			cerr << "Error: line counts are not equal" << endl;
			return 1;
		}
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
	size_t height = file_count + BANNER_HEIGHT;

	Image image(Geometry(width, height), Color("black"));
	image.type(TrueColorType);
	image.modifyImage();

	// Write banner text
	image.textAntiAlias(true);
	image.fontPointsize(48); // about 64px
	image.fontFamily(FONT_FAMILY);
	// white text
	image.fillColor(Color("white"));
	image.annotate(graph_title + " " + last_log_time_str, Magick::Geometry(0, 0, 0, 0), Magick::NorthWestGravity);

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
			size_t pos = line.find(",");
			string freq = line.substr(0, pos);
			string power = line.substr(pos + 1, line.length() - pos - 1);
			//cout << "Freq: " << freq << " Power: " << power << endl;
			// convert the power in dBm (-120dBm to 0dBm) to a color
			// display range is -20 to -120 dBm (reduced dynamic range for better visibility)
			double power_dBm = std::stod(power);
			if(power_dBm >= 0 || power_dBm < -120)
			{
				cerr << "Error: power out of range: " << power_dBm << endl;
				return 1;
			}
			const auto mappedcolor = tinycolormap::GetColor((power_dBm + 120) / 100, tinycolormap::ColormapType::Cubehelix);
			const Color color(mappedcolor.r() * MaxRGB, mappedcolor.g() * MaxRGB, mappedcolor.b() * MaxRGB, MaxRGB);
			image.pixelColor(x, i + BANNER_HEIGHT, color);
		}

		log_file.close();
	}

	// write the image to a file
	cerr << "Writing image to " << output_name << " (" << width << 'x' << height << ")" << endl;
	image.write(output_name);

	return 0;
}