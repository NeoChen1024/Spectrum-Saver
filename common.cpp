#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <chrono>
#include <unistd.h>
#include <limits.h>
#include <omp.h>
#include <date/date.h>
#include "common.hpp"
#include "config.hpp"

const time_point<system_clock> now(void)
{
	return system_clock::now();
}

const string time_str(void)
{
	return format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(now()));
}

const time_point<system_clock> time_from_str(const string &str)
{
	using date::parse;

	time_point<system_clock, seconds> time;
	std::istringstream ss(str);
	ss >> parse("%4Y%2m%2dT%2H%2M%2S", time);
	if_error(ss.fail(), "Failed to parse time string");

	return time;
}

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
	size_t in_record_line_count = 0;
	size_t real_line_count = 0;
	size_t lines_per_record = SIZE_MAX;

	// types of lines:
	// 	record header: # <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
	// 	data: <dbm>\n<dbm>\n<dbm>\n...
	// 	trailing newline of a record: \n
	// any other line is invalid

	while(getline(logfile_stream, line))
	{
		real_line_count++;
		if(line[0] == '#')
			continue; // comment line

		in_record_line_count++;

		// parse header
		if(in_record_line_count % lines_per_record == 1)
		{
			bool ret = parse_header(line, h);
			if_error(!ret, format("Error: invalid header at line #{}", real_line_count));

			if(first_header.steps == 0)
			{
				lines_per_record = h.steps + 2; // +1 for header, +1 for trailing newline
				first_header = h;
			}
			else
			{
				if_error(h.start_freq != first_header.start_freq,
					format("Error: start_freq mismatch at line #{}: {} != {}",
						real_line_count, h.start_freq, first_header.start_freq));
				if_error(h.stop_freq != first_header.stop_freq,
					format("Error: stop_freq mismatch at line #{}: {} != {}",
						real_line_count, h.stop_freq, first_header.stop_freq));
				if_error(h.steps != first_header.steps,
					format("Error: steps count mismatch at line #{}: {} != {}",
						real_line_count, h.steps, first_header.steps));
				if_error(h.rbw != first_header.rbw,
					format("Error: rbw mismatch at line #{}: {} != {}",
						real_line_count, h.rbw, first_header.rbw));
			}

			headers.emplace_back(h);
		}
		else if(in_record_line_count % lines_per_record == 0)
		{
			// trailing newline of a record
			if_error(!line.empty(), format("Error: newline expected at line #{}", real_line_count));
			in_record_line_count = 0;
		}
		else
		{
			float power = 0;
			// data line
			try
			{
				power = std::stof(line);
			}
			catch(const std::exception& e)
			{
				cerr << format("std::stod exception: {}\n", e.what());
				if_error(true, format("Error: failed to parse double from line {}: \"{}\"", real_line_count, line));
			}
			if(!isfinite(power))
				if_error(true, format("Error: invalid power value at line #{}", real_line_count));
			power_data.emplace_back(power);
		}
	}

	if_error(headers.size() == 0, "Error: no valid record found in log file");

	// check if size of power_data is correct
	if(power_data.size() != headers.size() * first_header.steps)
		if_error(true, "Error: power_data count is not correct");
}

// check for time consistency of log file
bool check_logfile_time_consistency(const vector<logheader_t> &headers, logproblem_t &problems)
{
	size_t inconsistency_count = 0;
	const auto record_count = headers.size();
	const auto first_sweep_time = time_from_str(headers.front().start_time);
	const auto last_sweep_time = time_from_str(headers.back().start_time);
	const auto time_diff = duration_cast<seconds>(last_sweep_time - first_sweep_time);
	const int interval = time_diff.count() / (record_count - 1);

	// check if time difference (in seconds) is divisible by record count
	// record_count - 1 == number of intervals
	if(time_diff.count() % (record_count - 1) != 0)
	{
		cerr << format("Warning: time range in seconds ({}) is not divisible by record count ({})\n",
			time_diff.count(), record_count);
		problems.time_range_not_divisible_by_record_count = true;
		inconsistency_count++;
	}

	// check if interval is a factor of 60
	if(60 % interval != 0)
	{
		cerr << format("Warning: time interval {}sec is not a factor of 60\n", interval);
		problems.interval_not_divisible_by_60 = true;
		inconsistency_count++;
	}

	// check timing
	ssize_t last_interval = interval; // for checking if interval is constant
	for(size_t i = 0; i < record_count - 1; i++)
	{
		const auto ts1 = time_from_str(headers.at(i).start_time);
		const auto te1 = time_from_str(headers.at(i).end_time);
		const auto ts2 = time_from_str(headers.at(i + 1).start_time);
		const auto te2 = time_from_str(headers.at(i + 1).end_time);
		const auto tsdiff = duration_cast<seconds>(ts2 - ts1);

		// check for time overlap
		if(! (ts1 <= te1 && te1 <= ts2 && ts2 <= te2 && ts1 < ts2))
		{
			cerr << format("Warning: timestamp overlap between record #{} and #{}\n",
				i + 1, i + 2);
			problems.time_overlap = true;
			inconsistency_count++;
		}
		// end time earlier than start time
		if(te1 < ts1)
		{
			cerr << format("Warning: end time is earlier than start time in record #{}\n",
				i + 1);
			problems.time_overlap = true;
			inconsistency_count++;
		}
		// check the last record
		if(i == record_count - 2 && te2 < ts2)
		{
			cerr << format("Warning: end time is earlier than start time in record #{}\n",
				i + 2);
			problems.time_overlap = true;
			inconsistency_count++;
		}
	
		// check if time difference is constant
		const auto diff = tsdiff.count();
		if(diff != last_interval)
		{
			cerr << format("Warning: interval between record #{} and #{} changed from {}s to {}s\n",
				i + 1, i + 2, last_interval, diff);
			problems.variant_interval = true;
			inconsistency_count++;
		}
		if(diff < 0)
		{
			cerr << format("Warning: negative interval between record #{} and #{}\n",
				i + 1, i + 2);
			problems.negative_interval = true;
			inconsistency_count++;
		}

		last_interval = diff;
	}

	if(inconsistency_count > 0)
	{
		cerr << format("{} inconsistency(s) found, may not be able perform operations involving time correctly.\n",
			inconsistency_count);
	}


	return inconsistency_count > 0;
}
