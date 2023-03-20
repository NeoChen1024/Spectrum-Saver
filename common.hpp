#include <ctime>
#include <string>
#include <chrono>

#define FMT_HEADER_ONLY
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/ostream.h>
#include <fmt/std.h>

using std::string;
using std::cerr;
using std::endl;

using fmt::format;
using fmt::print;

static auto now(void)
{
	return std::chrono::system_clock::now();
}

static const string time_str(void)
{
	return format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(now()));
}

void static if_error(bool condition, const string &message)
{
	if(condition)
	{
		cerr << message << endl;
		exit(1);
	}
}

typedef struct log_header
{
	double start_freq;
	double stop_freq;
	size_t steps;
	float rbw;
	string start_time;
	string end_time;
} log_header_t;
