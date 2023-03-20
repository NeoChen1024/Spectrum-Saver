/*
 *   spsave: Save spectrum data from tinySA / tinySA Ultra to log files
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
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <ctime>
#include <chrono>
#include <thread>
#include "common.hpp"
#include "config.hpp"

using std::string;
using std::cout;
using std::cerr;
using std::flush;
using std::endl;
using std::atof;
using std::stringstream;
using std::fstream;
using std::to_string;

int send_cmd(int fd, string cmd)
{
	// Send commands though fd
	//cerr << "<< " << cmd << endl;
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
	// remove the last 4 characters
	response.erase(response.length() - 4);

	cout << ">> " << response << endl;
	return response;
}

// TODO: the argument list is becoming too long, consider using a struct
const string read_scanraw(
	int fd, int zero_level, log_header_t &h, fstream &output)
{
	string response;
	uint8_t c;
	
	cout << format("[{}] Reading... ", time_str()) << flush;
	while(read(fd, &c, 1) > 0)
	{
		response += c;
		// If we get a 'ch> ' prompt, we're done
		if(response.length() >= 4 && response.substr(response.length() - 4) == "ch> ")
			break;
	}

	// count x & print out CSV
	int x_count = 0;
	// make data header
	// # <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
	output << format("# {:.06f},{:.06f},{},{:.03f},{},{}\n",
		h.start_freq, h.stop_freq, h.steps, h.rbw, h.start_time, time_str());
	// first '{' + 1 is x
	for(unsigned int i = response.find_first_of('{') + 1; i < response.length(); i += 3)
	{
		if(response[i] == 'x')
		{
			uint16_t data;
			// x_count starts from 0
			x_count++;
			data = response[i+1] & 0xff; // avoid sign extension
			data |= response[i+2] << 8;
			// freq in MHz, data in dBm
			output << format("{:.1f}\n", data / 32.0 - zero_level);
		}
		else
		{
			break;
		}
	}
	output << endl; // one empty line between each scan
	cout << format("Done. {} points read.\t", x_count) << flush; // don't do newline here
	return response;
}

// Credits: https://stackoverflow.com/questions/54591636/ceiling-time-point-to-runtime-defined-duration/54634050#54634050
template <class Clock, class Duration1, class Duration2>
constexpr auto ceil(std::chrono::time_point<Clock, Duration1> t, Duration2 m) noexcept
{
	using R = std::chrono::time_point<Clock, Duration2>;
	auto r = std::chrono::time_point_cast<Duration2>(R{} + (t - R{}) / m * m);
	if (r < t)
		r += m;
	return r;
}

auto awake_time(int interval)
{
	std::chrono::time_point<std::chrono::system_clock> next;
	
	return ceil(now(), std::chrono::seconds(interval));
}

void help_msg(char *argv[])
{
	cout << "Usage: " << argv[0] << " [options]" << endl <<
		"\t-t <ttydev>\n"
		"\t-m <tinySA Model>	\"tinySA\" or \"tinySA4\" (default)\n"
		"\t-s <start freq MHz>	default: 1\n"
		"\t-e <stop freq MHz>	default: 30\n"
		"\t-k <step freq kHz>	default: 10\n"
		"\t-r <RBW in kHz>\t	default: 10, consult tinySA.org for supported RBW values\n"
		"\t-p <filename prefix>	default \"sp\"\n"
		"\t-l <loop?>		0 is false (default), any other value is true\n"
		"\t-i <interval>\t	sweep interval in seconds (default: 60)" << endl << endl;
}

const string new_logfile(fstream &output, const string &filename_prefix, const string &start_time)
{
	const string filename = {filename_prefix + '.' + start_time + ".log"};
	if(output.is_open())
		output.close();
	output.open(filename, std::ios::out);
	if_error(!output.is_open(), "Error: cannot open output file");

	return filename;
}

int main(int argc, char *argv[])
{

	string ttydev = "";
	double step_freq_kHz = 10;
	log_header_t h =
	{
		.start_freq = 1,
		.stop_freq = 30,
		.steps = 2901,
		.rbw = 10,
		.start_time = "",
		.end_time = ""
	};
	string filename_prefix = "sp";
	bool loop = 0; // whether to run in a loop or not
	int interval = 60; // interval in seconds
	string model = "tinySA4"; // tinySA or tinySA4 (Ultra)

	// Parse arguments
	int opt;
	while((opt = getopt(argc, argv, "t:s:e:k:r:p:l:i:m:h")) != -1)
	{
		switch(opt)
		{
			case 't':
				ttydev = optarg;
				break;
			case 's':
				h.start_freq = atof(optarg);
				break;
			case 'e':
				h.stop_freq = atof(optarg);
				break;
			case 'k':
				step_freq_kHz = atof(optarg);
				break;
			case 'r':
				h.rbw = atof(optarg);
				break;
			case 'p':
				filename_prefix = optarg;
				break;
			case 'l':
				loop = atoi(optarg) == 0 ? false : true;
				break;
			case 'i':
				interval = atoi(optarg);
				if(60 % interval != 0)
				{
					cerr <<
						"Warning: interval " + to_string(interval) +
						" is not a factor of 60, correct behavior of log2png is not guaranteed"
						<< endl;
				}
				break;
			case 'm':
				model = optarg;
				break;
			case 'h':
			default:
				help_msg(argv);
				return 1;
		}
	}

	// Sanity check
	if_error(h.start_freq >= h.stop_freq, "Error: start freq > stop freq");
	if_error(ttydev.empty(), "Error: no tty device specified");

	// Open the serial port
	int fd = open(ttydev.c_str(), O_RDWR | O_NOCTTY);
	if_error(!isatty(fd), "Error: " + ttydev + " is not a tty");
	// set baudrate to 115200 8N1, no flow control, no modem control, no echo & CR/LF translation
	struct termios tty;
	tcgetattr(fd, &tty);
	cfsetospeed(&tty, B115200);
	cfsetispeed(&tty, B115200);
	tty.c_cflag &= ~PARENB; // no parity
	tty.c_cflag &= ~CSTOPB; // 1 stop bit
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8; // 8 bits
	tty.c_cflag &= ~CRTSCTS; // no flow control
	tty.c_cflag |= CREAD | CLOCAL; // turn on READ & ignore ctrl lines
	tty.c_lflag &= ~ICANON; // no canonical mode
	tty.c_lflag &= ~ECHO; // no echo
	tty.c_lflag &= ~ECHOE; // no echo erase
	tty.c_lflag &= ~ECHONL; // no echo new line
	tty.c_lflag &= ~ISIG; // no interpretation of INTR, QUIT and SUSP
	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // no software flow control
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // no any special handling of received bytes
	tty.c_oflag &= ~OPOST; // no output processing
	tty.c_oflag &= ~ONLCR; // no CR -> NL translation
	tty.c_cc[VTIME] = 0; // no timeout
	tty.c_cc[VMIN] = 1; // no minimum number of bytes to read
	tcsetattr(fd, TCSANOW, &tty);

	cerr << format("tty = {}, start = {:.6f}MHz, stop = {:.6f}MHz, step = {:.3f}kHz, rbw = {:.3f}kHz, filename prefix = \"{}\"\n",
		ttydev, h.start_freq, h.stop_freq, step_freq_kHz, h.rbw, filename_prefix);

	print("Initializing...\n\n");
	// Send init command
	send_cmd(fd, "");
	read_response(fd);
	send_cmd(fd, "pause");
	read_response(fd);
	//send_cmd(fd, "rbw "+ to_string(h.rbw));
	send_cmd(fd, format("rbw {:.1f}", h.rbw));
	read_response(fd);

	print("Sweeping...\n\n");
	// Calculate the number of steps
	double steps_floating = (h.stop_freq - h.start_freq) / (step_freq_kHz / 1e3)  + 1;
	if(ceil(steps_floating) != steps_floating)
		print("Warning: the number of steps will not be an integer, the actual number of steps would be {}\n", ceil(steps_floating));
	h.steps = ceil(steps_floating);
	// construct the sweep command
	const string scanraw_cmd = format("scanraw {:.0f} {:.0f} {}", h.start_freq * 1e6, h.stop_freq * 1e6, h.steps);
	
	fstream output;
	string start_time = time_str();
	h.start_time = start_time;
	string filename = new_logfile(output, filename_prefix, start_time);

	int zero_level = ZERO_LEVEL_ULTRA;
	if(model == "tinySA")
		zero_level = ZERO_LEVEL;
	else if(model == "tinySA4")
		zero_level = ZERO_LEVEL_ULTRA;
	else
		if_error(true, "Error: unknown model " + model);

	// number of records written to file, will rotate file when it reaches MAX_RECORDS
	size_t record_count = 0;

	// initiate sweep
	if(loop)
	{
		while(1)
		{
			cout << format("\r[{:8d}] ", record_count) << flush;
			std::this_thread::sleep_until(awake_time(interval));
			start_time = time_str();
			h.start_time = start_time;
			send_cmd(fd, scanraw_cmd);
			read_scanraw(fd, zero_level, h, output);
			record_count++;

			// rotate file
			if(record_count >= MAX_RECORDS)
			{
				record_count = 0;
				// old log file will be closed in new_logfile()
				filename = new_logfile(output, filename_prefix, time_str());
				print("\n\nNew log file: {}\n", filename);
			}
		}
	}
	else
	{
		send_cmd(fd, scanraw_cmd);
		read_scanraw(fd, zero_level, h, output);
		send_cmd(fd, "resume");
	}
	output.close();
	cout << endl;

	return 0;
}
