# Spectrum Saver

A set of program for logging RSSI data from tinySA & render the logs into spectrogram

**WARNING: It's still WIP, code quality is eye-burning.**

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


 $ log2png [-f <log file>] [-p <filename prefix>] [-t <graph title>]
```

### Example of rendered spectrogram:

![](https://github.com/NeoChen1024/Spectrum-Saver/raw/trunk/pic/fmbc.png "Spectrogram")

### Spectrum Log Format:

```csv
# <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
<dBm>
<dBm>
<dBm>
...
<dBm>

# <start_freq>,<stop_freq>,<steps>,<RBW>,<start_time>,<end_time>
<dBm>
...
<dBm>
```
