/*
 *   spsave: Save spectrum data from tinySA Ultra to log files
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

	cerr << ">> " << response << endl;
	return response;
}

const string read_scanraw(int fd, double start_freq, double stop_freq, long int steps, string filename_prefix)
{
	string response;
	uint8_t c;
	
	fstream output;
	output.open(filename_prefix + "." + time_str() + ".log",
		std::fstream::out | std::fstream::app);

	fprintf(stderr, "[%s] Reading scanraw...", time_str().c_str());
	while(read(fd, &c, 1) > 0)
	{
		response += c;
		// If we get a 'ch> ' prompt, we're done
		if(response.length() >= 4 && response.substr(response.length() - 4) == "ch> ")
			break;
	}

	// count x & print out CSV
	int x_count = 0;
	// make CSV header
	output << "# freq(MHz),RSSI(dBm)" << endl;
	// first '{' + 1 is x
	for(unsigned int i = response.find_first_of('{') + 1; i < response.length(); i += 3)
	{
		if(response[i] == 'x')
		{
			uint16_t data;
			// x_count starts from 0
			double freq = start_freq + (stop_freq - start_freq) / (steps - 1) * x_count;
			x_count++;
			data = response[i+1] & 0xff; // avoid sign extension
			data |= response[i+2] << 8;
			// freq in MHz, data in dBm
			char str[256];
			sprintf(str, "%.06f,%f\n", freq / 1e6, data / 32.0 - ZERO_LEVEL); // see config.hpp
			output << str;
		}
		else
		{
			break;
		}
	}
	output.close();
	fprintf(stderr, "  DONE!\n");
	return response;
}

auto now() { return std::chrono::system_clock::now(); }
 
auto awake_time() {
    using std::chrono::operator""min;
    auto current_time = now();
    return std::chrono::ceil<std::chrono::minutes>(current_time);
}

void help_msg(char *argv[])
{
	//cout << "Usage: " << argv[0] << " <ttydev> <start freq MHz> <stop freq MHz> <step freq kHz> <RBW> <filename prefix> <loop?>" << endl;
	cout << "Usage: " << argv[0] << "-t <ttydev> -s <start freq MHz> -e <stop freq MHz> -k <step freq kHz> -r <RBW> -p <filename prefix> -l <loop?>" << endl;
}

int main(int argc, char *argv[])
{

	string ttydev = "";
	double start_freq = 1 * 1e6;	// argument in MHz, but use Hz internally
	double stop_freq = 30 * 1e6;	// same as above
	float step_freq = 10 * 1e3;	// argument in kHz, but use Hz internally
	float rbw = 10;	// RBW in kHz
	string filename_prefix = "sp";
	int loop = 0; // whether to run in a loop or not

	// Parse arguments
	int opt;
	while((opt = getopt(argc, argv, "t:s:e:k:r:p:l:")) != -1)
	{
		switch(opt)
		{
			case 't':
				ttydev = optarg;
				break;
			case 's':
				start_freq = atof(optarg) * 1e6;
				break;
			case 'e':
				stop_freq = atof(optarg) * 1e6;
				break;
			case 'k':
				step_freq = atof(optarg) * 1e3;
				break;
			case 'r':
				rbw = atof(optarg);
				break;
			case 'p':
				filename_prefix = optarg;
				break;
			case 'l':
				loop = atoi(optarg);
				break;
			default:
				help_msg(argv);
				return 1;
		}
	}

	fprintf(stderr, "tty = %s, ", ttydev.c_str());
	fprintf(stderr, "start = %3.06fMHz,\t", start_freq / 1e6);
	fprintf(stderr, "stop = %3.06fMHz,\t", stop_freq / 1e6);
	fprintf(stderr, "step = %.03fkHz,\t", step_freq / 1e3);
	fprintf(stderr, "rbw = %.03fkHz,\t", rbw);
	fprintf(stderr, "filename prefix = \"%s\"\n", filename_prefix.c_str());

	// Sanity check
	test_error(start_freq > stop_freq, "start freq > stop freq");

	// Open the serial port
	int fd = open(ttydev.c_str(), O_RDWR | O_NOCTTY);
	if(!isatty(fd))
	{
		fprintf(stderr, "Error: %s is not a tty\n", ttydev.c_str());
		return 1;
	}
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

	cerr << "Initializing..." << endl << endl;
	// Send init command
	send_cmd(fd, "");
	read_response(fd);
	send_cmd(fd, "pause");
	read_response(fd);
	send_cmd(fd, "rbw "+ to_string(rbw));
	read_response(fd);

	cerr << "Sweeping..." << endl << endl;
	// construct the sweep command
	stringstream ss;
	long int steps = (stop_freq - start_freq) / step_freq + 1;
	ss << "scanraw " << start_freq << " " << stop_freq << " " << steps;
	
	// initiate sweep
	if(loop)
	{
		while(1)
		{
			fprintf(stderr, "Sleeping  ");
			std::this_thread::sleep_until(awake_time());
			send_cmd(fd, ss.str());
			read_scanraw(fd, start_freq, stop_freq, steps, filename_prefix);
		}
	}
	else
	{
		send_cmd(fd, ss.str());
		read_scanraw(fd, start_freq, stop_freq, steps, filename_prefix);
	}

	return 0;
}
