# Spectrum Saver

A set of program for logging RSSI data from tinySA & render the logs into spectrogram

**WARNING: It's still WIP, code quality is eye-burning.**

### Building:

```shell
$ make
```

### Usage:

```shell
 $ spsave <tty device> <start freq in MHz> <end freq in MHz> <freq step in kHz> <RBW> <filename prefix> <loop?>
 $ log2png <log dir> <png filename prefix> "Banner Title" 
```

### Example of rendered spectrogram:

![](https://github.com/NeoChen1024/Spectrum-Saver/raw/trunk/pic/sp.20230316T121059.png "Spectrogram")

### TODO:

New better log format:

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
