#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <ctime>
#include <chrono>
#include <thread>

using std::string;
using std::cout;
using std::endl;
using std::stringstream;
using std::fstream;

int send_cmd(int fd, string cmd)
{
	// Send commands though fd
	cmd += "\r";

	write(fd, cmd.c_str(), cmd.length());
	return 0;
}

const string read_response(int fd)
{
	// Read response from fd

	string response;
	char c;
	while(read(fd, &c, 1) > 0)
	{
		response += c;
		// If we get a 'ch> ' prompt, we're done
		//fprintf(stderr, "response (%hhx) = %s\n", c, response.c_str());
		if(response.length() >= 4 && response.substr(response.length() - 4) == "ch> ")
			break;
	}
	return response;
}

const string read_sweep(int fd, string filename_prefix)
{
	// Skip one line
	string response;
	char c;
	char time_str[256];
	time_t current_time = time(NULL);
	strftime(time_str, sizeof(time_str), "%Y%m%dT%H%M%S", gmtime(&current_time));

	fstream output;
	output.open(filename_prefix + "." + string(time_str) + ".log",
		std::fstream::out | std::fstream::app);

	while(read(fd, &c, 1) > 0)
	{
		// Get the last line
		if(c == '\n')
		{
			if(!response.starts_with("hop "))
			{
				double freq = 0;
				float power = 0;
				char str[256];

				current_time = time(NULL);
				strftime(time_str, sizeof(time_str), "%Y%m%dT%H%M%S", gmtime(&current_time));

				sscanf(response.c_str(), "%lf %f", &freq, &power);
				sprintf(str, "%2.06lf,%#+3.01f", freq / 1e6, power);
				output << str << endl;

				fprintf(stderr, "\r[%s] %s\t\t", time_str, str);
			}
			response = "";
		}
		else
			response += c;
		// If we get a 'ch> ' prompt, we're done
		if(response.length() >= 4 && response.substr(response.length() - 4) == "ch> ")
		{
			break;
		}
	}
	output.close();
	fputs("\n", stderr);
	return response;
}

auto now() { return std::chrono::steady_clock::now(); }
 
auto awake_time() {
    using std::chrono::operator""min;
    auto current_time = now();
    return std::chrono::floor<std::chrono::minutes>(current_time) + 3min;
}

int main(int argc, char *argv[])
{
	if(argc < 6)
	{
		cout << "Usage: " << argv[0] << " <ttydev> <start freq MHz> <stop freq MHz> <step freq kHz> <filename prefix>" << endl;
		return 1;
	}

	string ttydev = argv[1];
	long int start_freq = atol(argv[2]) * 1000 * 1000;
	long int stop_freq = atol(argv[3]) * 1000 * 1000;
	int step_freq = atoi(argv[4]); // kHz
	string filename_prefix = argv[5];

	fprintf(stderr, "tty = %s, ", ttydev.c_str());
	fprintf(stderr, "start = %ld, ", start_freq);
	fprintf(stderr, "stop = %ld, ", stop_freq);
	fprintf(stderr, "step = %d, ", step_freq);
	fprintf(stderr, "filename prefix = %s\n", filename_prefix.c_str());

	// Open the serial port
	int fd = open(ttydev.c_str(), O_RDWR | O_NOCTTY);
	if(!isatty(fd))
	{
		fprintf(stderr, "Error: %s is not a tty\n", ttydev.c_str());
		return 1;
	}

	// Send init command
	send_cmd(fd, "pause");
	read_response(fd);
	send_cmd(fd, "rbw 30");
	read_response(fd);

	// construct the sweep command
	stringstream ss;
	ss << "hop " << start_freq << " " << stop_freq << " " << step_freq * 1000;
	
	// initiate sweep
	while(1)
	{
		fprintf(stderr, "Sleeping");
		std::this_thread::sleep_until(awake_time());
		send_cmd(fd, ss.str());
		read_sweep(fd, filename_prefix);
	}

	return 0;
}
