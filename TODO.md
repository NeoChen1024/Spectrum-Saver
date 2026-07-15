# TODO

The C++20 migration, correctness fixes, ImageMagick pixel-view cleanup, and
baseline parser/protocol tests were handled by the first modernization change
set. The binary-format change set has now added the version 1.0 codec, Google
CRC32C, text/binary reader and writer abstractions, a bounded asynchronous
acquisition writer, format auto-detection, and `splogconvert`.

The following work remains after the core implementation.

## 1. Streaming and recovery readers

- Change validation and conversion to consume one record at a time. The current
  `LogReader` interface still accumulates the complete renderer data set.
- Add an explicit damaged-record recovery mode that scans for the next record
  marker and validates candidate length, invariants, and CRC32C. Normal reading
  already retains complete records before a truncated final record.
- Add sequence-gap diagnostics to recovery mode instead of treating every gap
  as fatal.
- Preserve indefinite support for existing text logs and their strict
  `std::from_chars` diagnostics.

## 2. Binary format follow-up

Version 1.0 is specified in
[`docs/binary-log-format.md`](docs/binary-log-format.md). It defines the exact
file and record byte layouts, integer affine calibration, metadata TLVs,
mandatory record CRC32C, interrupted-tail handling, and recovery behavior.

- Decide whether later minor versions need additional metadata types before
  assigning more identifiers.
- Consider avoiding the temporary encoded payload buffer on little-endian
  hosts after measuring whether it matters.

## 3. Asynchronous acquisition follow-up

- Expose the current 16-record queue limit as a CLI setting if field testing
  shows a need to tune it.
- Report queue high-water marks and sustained backpressure to the user.
- Decide whether a configurable `fdatasync` interval is needed; ordinary stream
  flush is not a power-loss durability guarantee.
- Consider parsing `scanraw` directly from chunked serial reads into the owned
  sample vector, avoiding the current intermediate response buffer and
  one-byte `read()` calls.

## 4. CLI and compatibility

- Reconsider whether binary should become the acquisition default after a
  compatibility period. Text is currently still the default.

## 5. Validation and tests

- Cross-endian decoding tests.
- More unknown version, encoding, critical-extension, invalid-length, count
  mismatch, integer-overflow, and false synchronization-marker tests.
- Damaged middle-record recovery tests after recovery mode is implemented.
- Benchmarks for parsing throughput, peak memory, output size, and queue
  behavior using the repository example log.
