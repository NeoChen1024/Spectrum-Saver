// Reads all CSV log files in the current directory and outputs a PNG file with the spectrum

#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <Magick++.h>
#include <vector>
#include <algorithm>

int main(int argc, char *argv[])
{
	using std::cout;
	using std::endl;
	using std::string;
	using std::vector;
	using std::sort;
	using std::filesystem::directory_iterator;
	using std::filesystem::path;
	using namespace Magick;

	if(argc < 2)
	{
		cout << "Usage: log2png <dir>" << endl;
	}
	string dir = argv[1];

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
			cout << "Error: line counts are not equal" << endl;
			return 1;
		}
	}

	line_count -= 1; // subtract the header line

	size_t file_count = log_files.size(); // this will determine the height of the spectrogram
	cout << "Found " << file_count << " files with " << line_count << "measurements each" << endl;

	return 0;
}