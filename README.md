# Spectrum Saver

A set of program for logging spectrum from tinySA / tinySA Ultra & render the logs into spectrogram

**WARNING: It's still WIP, code quality is eye-burning.**

### Cloning:

```sh
 $ git clone --recursive https://github.com/NeoChen1024/Spectrum-Saver.git
```

### Dependencies:

* [{fmt}](https://github.com/fmtlib/fmt "GitHub repo") string formatting library
* Modern version of GCC or Clang for C++20 support

### Building:

```shell
$ make
```

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


 $ log2png -f <log file> [-p <filename prefix>] [-t <graph title>] [-g <grid?>]
```

### Example of rendered spectrogram:

![FM BC 87.5~108MHz Spectrogram](https://github.com/NeoChen1024/Spectrum-Saver/raw/trunk/pic/fmbc.png)

### Spectrum Log Format:

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

### Credits:

* [tinycolormap](https://github.com/yuki-koyama/tinycolormap "GitHub repo") for this awesome colormap library
* [date](https://github.com/HowardHinnant/date) for better date/time parsing function (hopefully will be included in C++23)
