# Spectrum Saver

A set of program for logging spectrum from tinySA / tinySA Ultra & render the logs into spectrogram

**WARNING: It's still WIP, code quality is eye-burning.**

### Cloning:

```sh
 $ git clone --recursive https://github.com/NeoChen1024/Spectrum-Saver.git
```

### Dependencies

- A C++20 compiler and standard library with `std::format` and C++20 chrono
  parsing support
- CMake 3.16 or newer and pkg-config
- [ImageMagick](https://imagemagick.org/) with Magick++ development files
- OpenMP
- [Google CRC32C](https://github.com/google/crc32c), included as a Git submodule
- [tinycolormap](https://github.com/yuki-koyama/tinycolormap), included as a
  Git submodule

### Building

```shell
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --parallel
$ ctest --test-dir build --output-on-failure
```

The programs are written to `build/`. To install them, run
`cmake --install build --prefix /path/to/install`. Tests can be disabled with
`-DBUILD_TESTING=OFF` during configuration.

### Usage:

```shell
 $ spsave [options]
	-t <ttydev>
	-m <tinySA Model>	"tinySA" or "tinySA4"
	-s <start freq MHz>
	-e <stop freq MHz>
	-k <step freq kHz>
	-r <RBW in kHz>		consult tinySA.org for supported RBW values
	-p <filename prefix>
	-l <loop?>		0 is false, any other value is true
	-i <interval>		sweep interval in seconds
	-x <max records>	default: 1440, 0 disables log rotation
	-F, --format <format>	text (default) or binary


 $ log2png -f <log file> [-p <filename prefix>] [-t <graph title>] [-g <grid?>]
	[--input-format auto|text|binary]


 $ splogconvert -f <input> -o <output> --output-format text|binary
	[--input-format auto|text|binary] [--model tinySA|tinySA4]
```

Text remains the default acquisition format during the compatibility period.
Binary acquisitions use the `.splog` extension and preserve the original
unsigned 16-bit device samples. Converting a legacy text log to binary requires
the device model because the old format does not record its calibration offset.
The binary format intentionally leaves records uncompressed to keep acquisition
latency, append behavior, and damaged-record recovery predictable.
During looping acquisition, `SIGINT` or `SIGTERM` finishes any active serial
response, resumes the device, drains accepted records, and closes the log.

### Example of rendered spectrogram:

![FM BC 87.5~108MHz Spectrogram](https://github.com/NeoChen1024/Spectrum-Saver/raw/trunk/pic/fmbc.png)

### Spectrum Log Format:

The current text format is shown below. The versioned binary format is
specified in [`docs/binary-log-format.md`](docs/binary-log-format.md).

```
# Optional comment
$ <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
<dBm>
<dBm>
<dBm>
...
<dBm>

$ <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
<dBm>
...
<dBm>
```

### Credits

* [Google CRC32C](https://github.com/google/crc32c) for the hardware-accelerated record integrity implementation
* [tinycolormap](https://github.com/yuki-koyama/tinycolormap "GitHub repo") for this awesome colormap library
