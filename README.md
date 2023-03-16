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
