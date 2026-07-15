# TODO

The C++20 migration, correctness fixes, ImageMagick pixel-view cleanup, and
baseline parser/protocol tests are handled by the first modernization change
set. The following work is intentionally deferred for a separate review.

## 1. Log reader and writer abstraction

- Introduce `LogReader` and `LogWriter` interfaces so acquisition, validation,
  and rendering do not depend on a specific on-disk representation.
- Move the current text implementation behind `TextLogReader` and
  `TextLogWriter` without changing the existing text format.
- Keep strict `std::from_chars` parsing and precise line/record diagnostics.
- Decide whether readers stream records or expose a complete in-memory data
  set. Rendering currently requires the complete image, but validation and
  conversion should remain streamable.
- Add format auto-detection without guessing from filename extensions.
- Preserve indefinite support for existing text logs.

## 2. Versioned binary log format

The format must be specified before implementation. Do not serialize native
C++ structs or rely on host padding, alignment, floating-point layout, or
endianness.

### File header

- Fixed magic value.
- Major and minor format versions.
- Explicit little-endian integer encoding.
- Header byte length and feature flags for forward-compatible extensions.
- Start and stop frequency as fixed-width integer Hz values.
- Resolution bandwidth as a fixed-width integer Hz value.
- Points per sweep.
- Sample encoding identifier.
- Calibration scale and offset metadata.
- Optional device/model and creation metadata in length-delimited fields.

### Record framing

- Synchronization marker suitable for locating the next record after damage.
- Total record byte length.
- Monotonic sequence number.
- Start and end timestamps as fixed-width Unix timestamps with a specified
  epoch and precision.
- Sample count, which must agree with both payload size and file metadata.
- Payload encoding identifier when per-record overrides are allowed.
- Optional CRC32C covering the record header and payload.
- Defined handling for a truncated final record so interrupted acquisition can
  retain all previously completed records.

### Initial sample encoding

- Prefer the original tinySA unsigned 16-bit samples in little-endian order.
- Store the conversion `power_dbm = raw * scale + offset` in file metadata;
  tinySA currently uses a scale of `1/32` and a model-dependent zero-level
  offset.
- Preserve the full device resolution instead of the text writer's current
  one-decimal-place rounding.
- Reserve encoding identifiers for calibrated IEEE-754 binary32 samples and
  future devices.

### Compression

- Measure uncompressed binary, whole-file compression, and independently
  compressed record blocks.
- Keep the base format readable without requiring compression.
- If compression is added, specify the codec and uncompressed length in a
  versioned, length-delimited container rather than inferring it externally.

### CLI and compatibility

- Add `spsave --format text|binary` only after the binary format is stable.
- Add `log2png --input-format auto|text|binary`.
- Decide whether text or binary remains the acquisition default during the
  compatibility period.
- Provide a text-to-binary conversion path and retain binary-to-text export for
  inspection and interoperability.

### Validation and tests

- Golden byte-level fixtures independent of the writer implementation.
- Text/binary round trips that produce identical calibrated samples.
- Cross-endian decoding tests.
- Unknown version, flags, encoding, and extension-field tests.
- Invalid lengths, count mismatches, integer overflow, CRC failure, and damaged
  synchronization-marker tests.
- Truncated-tail recovery tests.
- Render comparisons between equivalent text and binary inputs.
- Benchmarks for parsing throughput, peak memory, output size, and optional
  compression ratio using the repository example log.
