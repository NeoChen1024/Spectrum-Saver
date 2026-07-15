# Spectrum Saver Binary Log Format

## Status

This document specifies version 1.0 of the Spectrum Saver binary log format.
The format is intended to preserve the original samples returned by tinySA
devices, avoid floating-point text conversion and parsing, support streaming
writers and readers, and recover complete records from an interrupted or
partially damaged acquisition.

The keywords **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are
to be interpreted as normative requirements.

## Common encoding rules

- Multibyte integers MUST use little-endian byte order.
- Signed integers MUST use two's-complement representation.
- Writers MUST encode fields individually. They MUST NOT serialize native C++
  structures or expose host padding, alignment, or endianness.
- Byte offsets in this document are relative to the beginning of the structure
  being described.
- All lengths are in bytes and include only the components explicitly stated.
- Readers MUST use checked arithmetic before adding offsets or multiplying a
  sample count by its encoded width.
- Reserved fields MUST be written as zero and MUST be ignored by version 1.0
  readers after the containing structure has otherwise been validated.
- Unix timestamps are signed counts of nanoseconds since
  `1970-01-01T00:00:00Z`. They use POSIX time and therefore do not represent
  leap seconds.

Filename extensions are not part of the format. Readers MUST identify this
format from its file magic rather than from a filename.

## File header

Every file begins with one immutable file header. The fixed portion is followed
by zero or more metadata TLVs and one CRC32C value.

| Offset | Size | Type | Field |
| ---: | ---: | --- | --- |
| 0 | 8 | bytes | Magic: `53 50 53 4c 4f 47 0d 0a` (`SPSLOG\r\n`) |
| 8 | 2 | `uint16` | Major version; 1 for this specification |
| 10 | 2 | `uint16` | Minor version; 0 for this specification |
| 12 | 4 | `uint32` | `header_bytes`, including the final CRC32C |
| 16 | 4 | `uint32` | File feature flags; zero in version 1.0 |
| 20 | 2 | `uint16` | Sample encoding identifier |
| 22 | 2 | `uint16` | Reserved |
| 24 | 8 | `uint64` | Start frequency in Hz |
| 32 | 8 | `uint64` | Stop frequency in Hz |
| 40 | 4 | `uint32` | Resolution bandwidth in Hz |
| 44 | 4 | `uint32` | Points per sweep |
| 48 | 4 | `int32` | Calibration scale numerator |
| 52 | 4 | `int32` | Calibration offset numerator |
| 56 | 4 | `uint32` | Calibration denominator |
| 60 | 4 | `uint32` | Reserved |
| 64 | 8 | `int64` | File creation timestamp |
| 72 | variable | bytes | Metadata TLVs |
| `header_bytes - 4` | 4 | `uint32` | Header CRC32C |

`header_bytes` MUST be at least 76. The CRC32C covers bytes at offsets 0
through `header_bytes - 5`, inclusive. It does not cover its own four encoded
bytes.

The following invariants apply:

- Start frequency MUST be lower than stop frequency.
- Resolution bandwidth and points per sweep MUST be nonzero.
- The calibration scale numerator and denominator MUST be nonzero.
- A version 1.0 writer MUST set all file feature flags to zero.
- A version 1.0 reader MUST reject nonzero file feature flags.
- File header CRC failure is fatal because record samples cannot be interpreted
  safely without trustworthy frequency, encoding, and calibration metadata.

### Version handling

A reader MUST reject an unsupported major version. A reader MAY accept a newer
minor version of a supported major version only if all flags and critical
extensions are understood. Increased header lengths and unknown non-critical
metadata TLVs alone do not make a newer minor version unreadable.

## Metadata TLVs

Metadata TLVs occupy all bytes between the fixed 72-byte header and the final
CRC32C. TLVs are packed consecutively without alignment padding.

| Offset | Size | Type | Field |
| ---: | ---: | --- | --- |
| 0 | 2 | `uint16` | Metadata type |
| 2 | 2 | `uint16` | Metadata flags |
| 4 | 4 | `uint32` | Value length |
| 8 | variable | bytes | Value |

Bit 0 of the metadata flags is the critical bit. All other metadata flag bits
are reserved in version 1.0 and MUST be zero. A reader MUST reject an unknown
critical metadata type and MUST skip an unknown non-critical metadata type.

Version 1.0 defines these metadata types:

| Type | Name | Encoding |
| ---: | --- | --- |
| 1 | Device model | UTF-8 without a trailing NUL |
| 2 | Writer application | UTF-8 without a trailing NUL |
| 3 | Device identifier | UTF-8 without a trailing NUL |
| 4 | User comment | UTF-8 without a trailing NUL |

A version 1.0 writer MUST NOT emit the same defined metadata type more than
once. A reader MUST reject duplicate instances of a defined type. Metadata is
descriptive and MUST NOT override fixed header fields.

## Sample encoding and calibration

Sample encoding identifier 1 is `RAW_U16_AFFINE`: one unsigned 16-bit integer
per sample in little-endian order. It is the only sample encoding defined by
version 1.0. Identifier 0 is invalid; all other identifiers are reserved for
future specifications and MUST be rejected by a version 1.0 reader.

The calibrated power for `RAW_U16_AFFINE` is computed as:

```text
power_dbm =
    (raw_sample * calibration_scale_numerator
        + calibration_offset_numerator)
    / calibration_denominator
```

The multiplication and addition MUST be evaluated in a type wide enough to
avoid overflow before conversion to floating point. The current tinySA models
are represented exactly as follows:

| Model | Scale numerator | Offset numerator | Denominator | Equivalent formula |
| --- | ---: | ---: | ---: | --- |
| tinySA | 1 | -4096 | 32 | `raw / 32 - 128` |
| tinySA Ultra | 1 | -5568 | 32 | `raw / 32 - 174` |

This preserves the device's full sample resolution. In particular, it avoids
the existing text format's one-decimal-place rounding.

## Sweep records

The file header is immediately followed by zero or more sweep records. Each
record is independently framed and protected by a mandatory CRC32C.

| Offset | Size | Type | Field |
| ---: | ---: | --- | --- |
| 0 | 8 | bytes | Sync marker: `53 50 53 52 45 43 0d 0a` (`SPSREC\r\n`) |
| 8 | 4 | `uint32` | `record_bytes`, including the final CRC32C |
| 12 | 2 | `uint16` | `record_header_bytes`; 48 in version 1.0 |
| 14 | 2 | `uint16` | Record flags; zero in version 1.0 |
| 16 | 8 | `uint64` | Sequence number |
| 24 | 8 | `int64` | Sweep start timestamp |
| 32 | 8 | `int64` | Sweep end timestamp |
| 40 | 4 | `uint32` | Sample count |
| 44 | 4 | `uint32` | Payload byte length |
| 48 | variable | bytes | Sample payload |
| `record_bytes - 4` | 4 | `uint32` | Record CRC32C |

The record CRC32C covers bytes at offsets 0 through `record_bytes - 5`,
inclusive. It therefore protects the sync marker, record header, any future
record-header extensions, and the complete payload. It does not cover its own
four encoded bytes.

For version 1.0:

- `record_header_bytes` MUST equal 48.
- Record flags MUST be zero.
- Sequence numbers MUST begin at zero in each file and increase by one for each
  record written.
- The end timestamp MUST be greater than or equal to the start timestamp.
- Sample count MUST equal the file header's points per sweep.
- Payload byte length MUST equal `sample_count * 2`.
- `record_bytes` MUST equal `record_header_bytes + payload_bytes + 4`.
- Records MUST use the sample encoding and calibration declared by the file
  header. Version 1.0 has no per-record encoding override.

The file header intentionally contains neither a record count nor an index.
Writers therefore never need to seek backwards or mutate the header when
appending records.

## CRC32C

Both CRC fields use CRC32C with the Castagnoli polynomial, as specified for
iSCSI. The four-byte result is encoded as a little-endian `uint32`.

The reference implementation for Spectrum Saver is
[google/crc32c](https://github.com/google/crc32c). Implementations can use
`crc32c::Crc32c()` for a contiguous byte range and `crc32c::Extend()` to cover
a separately stored header and payload without concatenating them. The file
format is defined by the CRC32C algorithm and covered byte ranges, not by the
library's in-memory representation.

Independent implementations and golden fixtures SHOULD verify that the ASCII
byte sequence `123456789` has CRC32C value `0xe3069283`.

## Interrupted writes and damaged-record recovery

A sequential reader MUST retain every preceding record whose framing,
invariants, and CRC32C have been validated. If EOF occurs after any byte of a
new record but before that record is complete, the reader MUST classify it as a
truncated final record and MUST NOT expose it as a valid sweep.

A recovery reader MAY search for the next eight-byte record sync marker after
corruption. A marker is only a candidate; it MUST NOT be accepted until all of
the following checks succeed:

1. Header and total lengths are arithmetically valid and within implementation
   safety limits.
2. The complete candidate record is available.
3. Version 1.0 record flags, counts, payload length, and timestamps are valid.
4. The record CRC32C matches.

If a candidate fails, recovery continues one byte after the beginning of that
candidate marker. A sequence-number discontinuity SHOULD be reported so users
can identify missing records. Recovery MUST NOT be attempted when the file
header is invalid.

## Compression

Records and payloads are intentionally uncompressed. Individual sweep records
are small, so an integrated codec would add dependency, buffering, latency, and
recovery complexity without enough benefit. Readers MUST NOT infer compression
from filenames or feature flags.

Whole-file compression is outside this format and is not recommended for live
acquisition because it removes direct append, record-level recovery, and random
access to the underlying log. It MAY be applied externally to an inactive file
for transport or archival storage; format detection and recovery then operate
on the decompressed byte stream.

## Streaming and asynchronous writer requirements

These requirements describe the intended acquisition architecture rather than
additional bytes in the file format.

The serial acquisition path SHOULD produce a record containing its sequence,
timestamps, and owned `uint16` sample buffer, then move that record into a
bounded queue. A dedicated writer thread SHOULD own the output stream, format
serialization, file rotation, flushing, and writer error state.

The queue MUST be bounded by record count, total sample bytes, or both. The
default policy MUST NOT silently discard records. If the queue becomes full,
acquisition SHOULD wait before issuing the next scan command, never while a
serial response is only partially consumed. This isolates serial reads from
temporary filesystem stalls while applying explicit backpressure if storage is
slower than acquisition for a sustained period.

The writer MUST NOT hold the queue lock while performing file I/O. Writer
failures MUST close the producer side of the queue, wake blocked producers, and
propagate the original error to the acquisition thread.

Normal shutdown MUST close the producer side, drain all accepted records, close
the output file, and join the writer thread. It MUST NOT treat a thread stop
request as permission to silently discard queued records. File rotation MUST be
performed by the writer thread after completing a record.

Completing a `write()` or stream flush does not imply stable storage. Any
`fdatasync` or equivalent durability interval is an acquisition policy and is
not encoded in this format.

## Compatibility and implementation requirements

- Existing text logs remain supported indefinitely.
- Binary format detection uses the eight-byte file magic.
- Text-to-binary conversion cannot recover precision already lost to decimal
  rounding, but timestamps and represented calibrated values must be retained.
- Binary-to-text export is intended for inspection and interoperability and
  does not replace the binary source as the lossless archive.
- Rendering, validation, and conversion readers SHOULD stream complete records.
  A renderer MAY accumulate decoded values when its image construction requires
  the complete data set.

## Required validation

An implementation is not complete without:

- Golden byte fixtures constructed independently of the production writer.
- File-header and record CRC32C vectors.
- Text/binary calibrated-value equivalence tests.
- Explicit byte-swapped decoding tests on little-endian hosts.
- Unknown version, flags, encoding, and TLV tests.
- Invalid and overflowing length/count tests.
- CRC failure and false sync-marker tests.
- Truncated-tail and middle-record recovery tests.
- Sequence-gap diagnostics.
- Equivalent text/binary render comparisons.
- Benchmarks for parsing throughput, peak memory, output size, and queue
  behavior using the repository example log.
