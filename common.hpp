#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <unistd.h>
#include <limits.h>
#include <thread>
#include <omp.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/ostream.h>
#include <fmt/std.h>

using std::cin;
using std::cout;
using std::cerr;
using std::flush;
using std::endl;
using std::ios;
using std::string;
using std::vector;
using std::stringstream;
using std::fstream;
using std::istream;
using std::ostream;
using std::to_string;
using std::chrono::system_clock;
using std::chrono::time_point;
using std::chrono::seconds;
using std::chrono::duration_cast;

using fmt::format;
using fmt::print;

typedef struct
{
	double start_freq;
	double stop_freq;
	size_t steps;
	float rbw;
	string start_time;
	string end_time;
} logheader_t;

class StringException : public std::exception
{
public:
	StringException(const string &message) : message(message) {}
	virtual const char *what() const throw() { return message.c_str(); }
private:
	string message;
};


static void inline if_error(bool condition, const string &message)
{
	if(condition)
	{
		throw(StringException(message));
	}
}

const time_point<system_clock> now(void);
const string time_str(void);
const time_point<system_clock> time_from_str(const string &str);
void check_logfile_time_consistency(const vector<logheader_t> &headers);
