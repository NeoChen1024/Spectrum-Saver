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

// check for time consistency of log file
void check_logfile_time_consistency(const vector<logheader_t> &headers)
{
	bool problems_found = false;

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
		problems_found = true;
	}

	// check if interval is a factor of 60
	if(60 % interval != 0)
	{
		cerr << format("Warning: time interval {}sec is not a factor of 60\n", interval);
		problems_found = true;
	}

	// check timing
	size_t inconsistency_count = 0;
	ssize_t last_interval = interval; // for checking if interval is constant
	for(size_t i = 0; i < record_count - 1; i++)
	{
		const auto ts1 = time_from_str(headers.at(i).start_time);
		const auto ts2 = time_from_str(headers.at(i + 1).start_time);
		const auto te1 = time_from_str(headers.at(i).end_time);
		const auto te2 = time_from_str(headers.at(i + 1).end_time);
		const auto tsdiff = duration_cast<seconds>(ts2 - ts1);

		// check for time overlap
		if(ts1 > ts2 || te1 > te2 || ts1 > te2 || te1 > ts2)
		{
			cerr << format("Warning: timestamp overlap between record #{} and #{}\n",
				i + 1, i + 2);
			problems_found = true;
			inconsistency_count++;
		}
		// end time earlier than start time
		if(te1 < ts1)
		{
			cerr << format("Warning: end time is earlier than start time in record #{}\n",
				i + 1);
			problems_found = true;
			inconsistency_count++;
		}
		// check the last record
		if(i == record_count - 2 && te2 < ts2)
		{
			cerr << format("Warning: end time is earlier than start time in record #{}\n",
				i + 2);
			problems_found = true;
			inconsistency_count++;
		}
		
		// check if time difference is constant
		const auto diff = tsdiff.count();
		if(diff != last_interval)
		{
			cerr << format("Warning: interval between record #{} and #{} changed from {}s to {}s\n",
				i + 1, i + 2, last_interval, diff);
			problems_found = true;
			inconsistency_count++;
		}

		last_interval = diff;
	}

	if(inconsistency_count > 0)
	{
		cerr << format("Warning: {} inconsistency(s) found\n",
			inconsistency_count);
		problems_found = true;
	}

	if(problems_found)
		cerr << "Warning: Problems found, may not be able perform operations involving time correctly.\n";
}
