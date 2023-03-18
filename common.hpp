#include <ctime>
#include <string>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/format-inl.h>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/ostream.h>
#include <fmt/std.h>

using std::string;
using std::cerr;
using std::endl;

using fmt::format;
using fmt::print;

static const string time_str(void)
{
	char time_str[256];
	time_t current_time = time(NULL);
	strftime(time_str, sizeof(time_str), "%Y%m%dT%H%M%S", gmtime(&current_time));

	return string(time_str);
}

void static if_error(bool condition, const string &message)
{
	if(condition)
	{
		cerr << message << endl;
		exit(1);
	}
}
