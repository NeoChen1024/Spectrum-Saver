#include <ctime>
#include <string>

using std::string;

const string time_str(void)
{
	char time_str[256];
	time_t current_time = time(NULL);
	strftime(time_str, sizeof(time_str), "%Y%m%dT%H%M%S", gmtime(&current_time));

	return string(time_str);
}
