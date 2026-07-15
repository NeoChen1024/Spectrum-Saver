/*
 *   spsave: Save spectrum data from tinySA / tinySA Ultra to log files
 *   Copyright (C) 2023 Kelei Chen
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#include "common.hpp"
#include "config.hpp"
#include "log_io.hpp"
#include "serial_protocol.hpp"

#include <cerrno>
#include <charconv>
#include <csignal>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <getopt.h>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <termios.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view DEVICE_PROMPT{"ch> "};
volatile std::sig_atomic_t stop_requested = 0;

extern "C" void request_stop(const int) noexcept
{
	stop_requested = 1;
}

class FileDescriptor
{
public:
	explicit FileDescriptor(const int descriptor) noexcept : descriptor_{descriptor} {}
	~FileDescriptor()
	{
		if(descriptor_ >= 0)
			::close(descriptor_);
	}

	FileDescriptor(const FileDescriptor &) = delete;
	FileDescriptor &operator=(const FileDescriptor &) = delete;

	[[nodiscard]] int get() const noexcept { return descriptor_; }

private:
	int descriptor_;
};

[[noreturn]] void throw_system_error(const std::string_view operation)
{
	throw std::system_error(errno, std::generic_category(), std::string{operation});
}

template<typename T>
[[nodiscard]] T parse_option(const char *text, const std::string_view option_name)
{
	const std::string_view input{text};
	T value{};
	const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
	throw_if(error != std::errc{} || end != input.data() + input.size(),
		std::format("Invalid value for {}: \"{}\"", option_name, input));
	if constexpr(std::is_floating_point_v<T>)
		throw_if(!std::isfinite(value), std::format("Non-finite value for {}", option_name));
	return value;
}

void write_all(const int descriptor, const std::string_view data)
{
	std::size_t written = 0;
	while(written < data.size())
	{
		const auto result = ::write(descriptor, data.data() + written, data.size() - written);
		if(result < 0)
		{
			if(errno == EINTR)
				continue;
			throw_system_error("write to serial device");
		}
		throw_if(result == 0, "Serial device accepted zero bytes");
		written += static_cast<std::size_t>(result);
	}
}

void send_command(const int descriptor, const std::string_view command)
{
	std::string terminated{command};
	terminated.push_back('\r');
	write_all(descriptor, terminated);
}

[[nodiscard]] bool ends_with_prompt(const std::span<const std::uint8_t> response)
{
	if(response.size() < DEVICE_PROMPT.size())
		return false;
	for(std::size_t index = 0; index < DEVICE_PROMPT.size(); ++index)
	{
		if(response[response.size() - DEVICE_PROMPT.size() + index]
			!= static_cast<std::uint8_t>(DEVICE_PROMPT[index]))
			return false;
	}
	return true;
}

[[nodiscard]] std::vector<std::uint8_t> read_until_prompt(
	const int descriptor,
	const std::size_t maximum_size
)
{
	std::vector<std::uint8_t> response;
	response.reserve(std::min<std::size_t>(maximum_size, 4096));
	while(!ends_with_prompt(response))
	{
		std::uint8_t byte{};
		const auto result = ::read(descriptor, &byte, 1);
		if(result < 0)
		{
			if(errno == EINTR)
				continue;
			throw_system_error("read from serial device");
		}
		throw_if(result == 0, "Serial device closed before sending its prompt");
		response.emplace_back(byte);
		throw_if(response.size() > maximum_size, "Serial response exceeds the configured limit");
	}
	response.resize(response.size() - DEVICE_PROMPT.size());
	return response;
}

[[nodiscard]] std::string read_response(const int descriptor)
{
	const auto bytes = read_until_prompt(descriptor, 1024 * 1024);
	const std::string response{bytes.begin(), bytes.end()};
	std::cout << ">> " << response << '\n';
	return response;
}

[[nodiscard]] SweepRecord read_scanraw(
	const int descriptor,
	const std::size_t expected_points,
	const std::chrono::sys_time<std::chrono::nanoseconds> start_time
)
{
	throw_if(expected_points > (std::numeric_limits<std::size_t>::max() - 4096) / 3,
		"Requested scan is too large");
	const auto response = read_until_prompt(descriptor, expected_points * 3 + 4096);
	auto samples = parse_scan_response(response, expected_points);
	const auto end_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(now());
	return SweepRecord{
		.sequence = 0,
		.start_time = start_time,
		.end_time = end_time,
		.samples = std::move(samples)
	};
}

[[nodiscard]] auto awake_time(const int interval)
{
	const auto current = now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		current.time_since_epoch());
	const auto interval_duration = std::chrono::seconds{interval};
	auto result = std::chrono::sys_seconds{
		interval_duration * (elapsed.count() / interval)};
	if(result < current)
		result += interval_duration;
	return result;
}

void sleep_until_or_stop(const std::chrono::system_clock::time_point deadline)
{
	using namespace std::chrono_literals;
	while(stop_requested == 0)
	{
		const auto current = now();
		if(current >= deadline)
			return;
		std::this_thread::sleep_for(std::min(deadline - current,
			std::chrono::duration_cast<std::chrono::system_clock::duration>(250ms)));
	}
}

void install_signal_handlers()
{
	struct sigaction action{};
	action.sa_handler = request_stop;
	::sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if(::sigaction(SIGINT, &action, nullptr) < 0 || ::sigaction(SIGTERM, &action, nullptr) < 0)
		throw_system_error("install signal handler");
}

void help_message(const char *program)
{
	std::cout << "Usage: " << program << " [options]\n"
		"\t-t <ttydev>\n"
		"\t-m <tinySA Model>\t\"tinySA\" or \"tinySA4\" (default)\n"
		"\t-s <start freq MHz>\tdefault: 1\n"
		"\t-e <stop freq MHz>\tdefault: 30\n"
		"\t-k <step freq kHz>\tdefault: 10\n"
		"\t-r <RBW in kHz>\tdefault: 10\n"
		"\t-p <filename prefix>\tdefault: \"sp\"\n"
		"\t-l <loop?>\t\t0 is false, any other integer is true\n"
		"\t-x <max records>\tdefault: 1440, 0 disables rotation\n"
		"\t-F, --format <format>\ttext (default) or binary\n"
		"\t-i <interval>\t\tsweep interval in seconds (default: 60)\n\n";
}

void configure_serial_port(const int descriptor)
{
	termios tty{};
	if(::tcgetattr(descriptor, &tty) < 0)
		throw_system_error("tcgetattr");
	if(::cfsetospeed(&tty, B115200) < 0 || ::cfsetispeed(&tty, B115200) < 0)
		throw_system_error("set serial baud rate");

	tty.c_cflag &= ~PARENB;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8;
	tty.c_cflag &= ~CRTSCTS;
	tty.c_cflag |= CREAD | CLOCAL;
	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO;
	tty.c_lflag &= ~ECHOE;
	tty.c_lflag &= ~ECHONL;
	tty.c_lflag &= ~ISIG;
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
	tty.c_oflag &= ~OPOST;
	tty.c_oflag &= ~ONLCR;
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;
	if(::tcsetattr(descriptor, TCSANOW, &tty) < 0)
		throw_system_error("tcsetattr");
}
}

int main(int argc, char *argv[])
try
{
	std::string tty_device;
	double step_frequency_khz = 10;
	LogHeader header{
		.start_freq = 1,
		.stop_freq = 30,
		.steps = 2901,
		.rbw = 10,
		.start_time = {},
		.end_time = {}
	};
	std::string filename_prefix{"sp"};
	bool loop = false;
	int interval = 60;
	std::string model{"tinySA4"};
	std::size_t max_records = 1440;
	LogFormat output_format = LogFormat::text;

	const option long_options[]{
		{"format", required_argument, nullptr, 'F'},
		{"help", no_argument, nullptr, 'h'},
		{nullptr, 0, nullptr, 0}
	};
	int option;
	while((option = ::getopt_long(argc, argv, "t:s:e:k:r:p:l:i:m:x:F:h",
		long_options, nullptr)) != -1)
	{
		switch(option)
		{
			case 't': tty_device = optarg; break;
			case 's': header.start_freq = parse_option<double>(optarg, "start frequency"); break;
			case 'e': header.stop_freq = parse_option<double>(optarg, "stop frequency"); break;
			case 'k': step_frequency_khz = parse_option<double>(optarg, "step frequency"); break;
			case 'r': header.rbw = parse_option<float>(optarg, "RBW"); break;
			case 'p': filename_prefix = optarg; break;
			case 'l': loop = parse_option<int>(optarg, "loop") != 0; break;
			case 'i': interval = parse_option<int>(optarg, "interval"); break;
			case 'm': model = optarg; break;
			case 'x': max_records = parse_option<std::size_t>(optarg, "max records"); break;
			case 'F': output_format = parse_log_format(optarg, false); break;
			case 'h': help_message(argv[0]); return EXIT_SUCCESS;
			default: help_message(argv[0]); return EXIT_FAILURE;
		}
	}

	throw_if(header.start_freq >= header.stop_freq, "Error: start frequency must be lower than stop frequency");
	throw_if(header.start_freq < 0, "Error: start frequency cannot be negative");
	throw_if(step_frequency_khz <= 0, "Error: step frequency must be positive");
	throw_if(header.rbw <= 0 || header.rbw > 1000, "Error: RBW must be in the range (0, 1000]");
	throw_if(interval <= 0, "Error: interval must be positive");
	throw_if(tty_device.empty(), "Error: no tty device specified");
	int zero_level{};
	if(model == "tinySA")
		zero_level = ZERO_LEVEL;
	else if(model == "tinySA4")
		zero_level = ZERO_LEVEL_ULTRA;
	else if(stop_requested == 0)
		throw std::runtime_error(std::format("Error: unknown model {}", model));
	if(60 % interval != 0)
	{
		std::cerr << std::format(
			"Warning: interval {} is not a factor of 60; log2png timing may be irregular\n", interval);
	}

	const int descriptor = ::open(tty_device.c_str(), O_RDWR | O_NOCTTY);
	if(descriptor < 0)
		throw_system_error(std::format("open {}", tty_device));
	FileDescriptor serial{descriptor};
	throw_if(::isatty(serial.get()) == 0, std::format("Error: {} is not a tty", tty_device));
	configure_serial_port(serial.get());
	install_signal_handlers();

	std::cerr << std::format(
		"tty = {}, start = {:.6f}MHz, stop = {:.6f}MHz, step = {:.3f}kHz, "
		"rbw = {:.3f}kHz, filename prefix = \"{}\", format = {}\n",
		tty_device, header.start_freq, header.stop_freq, step_frequency_khz,
		header.rbw, filename_prefix, log_format_name(output_format));

	print("Initializing...\n\n");
	send_command(serial.get(), "");
	(void)read_response(serial.get());
	send_command(serial.get(), "pause");
	(void)read_response(serial.get());
	send_command(serial.get(), std::format("rbw {:.1f}", header.rbw));
	(void)read_response(serial.get());

	print("Sweeping...\n\n");
	const double floating_steps =
		(header.stop_freq - header.start_freq) / (step_frequency_khz / 1e3) + 1;
	throw_if(!std::isfinite(floating_steps) || floating_steps < 2
		|| floating_steps > static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
		"Calculated step count is outside the supported range");
	const double rounded_steps = std::ceil(floating_steps);
	if(rounded_steps != floating_steps)
		print("Warning: rounded non-integral step count up to {}\n", rounded_steps);
	header.steps = static_cast<std::size_t>(rounded_steps);
	const auto scanraw_command = std::format(
		"scanraw {:.0f} {:.0f} {}", header.start_freq * 1e6, header.stop_freq * 1e6, header.steps);

	const auto start_frequency_hz = static_cast<std::uint64_t>(
		std::llround(header.start_freq * 1e6));
	const auto stop_frequency_hz = static_cast<std::uint64_t>(
		std::llround(header.stop_freq * 1e6));
	const auto rbw_hz = static_cast<std::uint32_t>(std::llround(header.rbw * 1e3));
	LogFileMetadata metadata{
		.start_frequency_hz = start_frequency_hz,
		.stop_frequency_hz = stop_frequency_hz,
		.resolution_bandwidth_hz = rbw_hz,
		.points_per_sweep = static_cast<std::uint32_t>(header.steps),
		.calibration_scale_numerator = 1,
		.calibration_offset_numerator = -zero_level * 32,
		.calibration_denominator = 32,
		.creation_time = {},
		.device_model = model,
		.writer_application = "Spectrum Saver spsave",
		.device_identifier = tty_device,
		.user_comment = {}
	};
	throw_if(header.steps > std::numeric_limits<std::size_t>::max() / 2 / 16,
		"Asynchronous queue size overflows size_t");
	auto rotating_writer = std::make_unique<RotatingLogWriter>(
		filename_prefix, output_format, std::move(metadata), max_records);
	AsyncLogWriter output{std::move(rotating_writer), 16, header.steps * 2 * 16};

	if(loop)
	{
		std::uint64_t record_count = 0;
		while(stop_requested == 0)
		{
			std::cout << std::format("\r[{:8d}] ", record_count + 1) << std::flush;
			sleep_until_or_stop(awake_time(interval));
			if(stop_requested != 0)
				break;
			output.check();
			const auto start_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(now());
			std::cout << std::format("[{}] Reading... ", time_str()) << std::flush;
			send_command(serial.get(), scanraw_command);
			auto record = read_scanraw(serial.get(), header.steps, start_time);
			const auto points_read = record.samples.size();
			output.enqueue(std::move(record));
			std::cout << std::format("Done. {} points read.\t", points_read) << std::flush;
			++record_count;
		}
	}
	else
	{
		const auto start_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(now());
		std::cout << std::format("[{}] Reading... ", time_str()) << std::flush;
		send_command(serial.get(), scanraw_command);
		auto record = read_scanraw(serial.get(), header.steps, start_time);
		const auto points_read = record.samples.size();
		output.enqueue(std::move(record));
		std::cout << std::format("Done. {} points read.\t", points_read) << std::flush;
	}
	send_command(serial.get(), "resume");
	output.close();
	std::cout << '\n';
	return EXIT_SUCCESS;
}
catch(const std::exception &error)
{
	std::cerr << error.what() << '\n';
	return EXIT_FAILURE;
}
