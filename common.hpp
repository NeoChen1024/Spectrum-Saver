#include <ctime>
#include <string>
#include <chrono>

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

typedef struct log_header
{
	double start_freq;
	double stop_freq;
	size_t steps;
	float rbw;
	string start_time;
	string end_time;
} log_header_t;

class StringException : public std::exception
{
public:
	StringException(const string &message) : message(message) {}
	virtual const char *what() const throw() { return message.c_str(); }
private:
	string message;
};

void static inline if_error(bool condition, const string &message)
{
	if(condition)
	{
		throw(StringException(message));
	}
}

static auto now(void)
{
	return std::chrono::system_clock::now();
}

static const string time_str(void)
{
	return format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(now()));
}
