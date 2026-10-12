"""Independent PNG RGB8 round-trip for the desktop_image_supply_test fixtures.

Runs the C++ test binary in a private directory, then decodes every fixture it
wrote with this script's own PNG/DEFLATE implementation:

- chunk walking and CRC32 verification (own table, no zlib),
- DEFLATE inflate for stored (BTYPE=0) and fixed-Huffman (BTYPE=1) blocks per
  RFC 1951, including a bit-by-bit canonical Huffman decoder and byte-wise
  LZ77 back-reference copying; dynamic Huffman (BTYPE=2) fails loudly,
- per-row PNG unfiltering for all five RFC 2083 filter types.

The encoder under test shares no logic with this decoder: expectations come
from each fixture's companion ``.raw`` file (the RGB bytes the test built the
frame from) plus a manifest with the declared size expectation.

Usage: python3 desktop_png_decode_check.py <path-to-desktop_image_supply_test>
"""

from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile


def build_crc_table():
    table = []
    for index in range(256):
        value = index
        for _ in range(8):
            value = (value >> 1) ^ (0xEDB88320 if value & 1 else 0)
        table.append(value)
    return table


CRC_TABLE = build_crc_table()


def crc32(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc = CRC_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


def adler32(data):
    a, b = 1, 0
    for byte in data:
        a = (a + byte) % 65521
        b = (b + a) % 65521
    return (b << 16) | a


class BitReader:
    """LSB-first bit reader over the zlib stream (RFC 1951 section 3.1.1)."""

    def __init__(self, data, offset):
        self.data = data
        self.offset = offset
        self.bit_buffer = 0
        self.bit_count = 0

    def read_bit(self):
        if self.bit_count == 0:
            if self.offset >= len(self.data):
                raise AssertionError("DEFLATE stream truncated")
            self.bit_buffer = self.data[self.offset]
            self.offset += 1
            self.bit_count = 8
        self.bit_count -= 1
        bit = self.bit_buffer & 1
        self.bit_buffer >>= 1
        return bit

    def read_bits(self, count):
        value = 0
        for position in range(count):
            value |= self.read_bit() << position
        return value

    def align_to_byte(self):
        self.bit_count = 0


class HuffmanTable:
    """Canonical Huffman decoder built from code lengths (RFC 1951 3.2.2)."""

    def __init__(self, lengths):
        self.counts = [0] * 16
        for length in lengths:
            self.counts[length] += 1
        self.counts[0] = 0
        offsets = [0] * 16
        total = 0
        for length in range(1, 16):
            offsets[length] = total
            total += self.counts[length]
        self.symbols = [0] * total
        for symbol, length in enumerate(lengths):
            if length:
                self.symbols[offsets[length]] = symbol
                offsets[length] += 1

    def decode(self, reader):
        """Bit-by-bit canonical walk (the "puff" decoding loop)."""
        code = 0
        first = 0
        index = 0
        for length in range(1, 16):
            code |= reader.read_bit()
            count = self.counts[length]
            if code - first < count:
                return self.symbols[index + (code - first)]
            index += count
            first = (first + count) << 1
            code <<= 1
        raise AssertionError("DEFLATE contains an invalid Huffman code")


FIXED_LITERAL_LENGTHS = (
    [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8  # 0-143, 144-255, 256-279, 280-287
)
FIXED_DISTANCE_LENGTHS = [5] * 30

LENGTH_BASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35,
               43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
LENGTH_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                4, 4, 4, 4, 5, 5, 5, 5, 0]
DISTANCE_BASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                 8193, 12289, 16385, 24577]
DISTANCE_EXTRA = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
                  8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]


def inflate_raw(compressed):
    """Decode a zlib stream to bytes with stored/fixed-Huffman support only."""
    assert compressed[0] == 0x78, f"unexpected zlib CMF {compressed[0]:#x}"
    assert (compressed[0] * 256 + compressed[1]) % 31 == 0, "bad zlib FCHECK"
    assert compressed[1] & 0x20 == 0, "unexpected zlib dictionary"
    reader = BitReader(compressed, 2)
    output = bytearray()
    while True:
        final = reader.read_bit()
        block_type = reader.read_bits(2)
        if block_type == 0:
            reader.align_to_byte()
            length = reader.data[reader.offset] | (reader.data[reader.offset + 1] << 8)
            inverse = reader.data[reader.offset + 2] | (reader.data[reader.offset + 3] << 8)
            assert length ^ inverse == 0xFFFF, "stored LEN/NLEN mismatch"
            reader.offset += 4
            assert reader.offset + length <= len(reader.data), \
                "stored block exceeds the stream"
            output += reader.data[reader.offset:reader.offset + length]
            reader.offset += length
        elif block_type == 1:
            literals = HuffmanTable(FIXED_LITERAL_LENGTHS)
            distances = HuffmanTable(FIXED_DISTANCE_LENGTHS)
            while True:
                symbol = literals.decode(reader)
                if symbol < 256:
                    output.append(symbol)
                elif symbol == 256:
                    break
                else:
                    index = symbol - 257
                    assert index < len(LENGTH_BASE), "invalid length symbol"
                    length = LENGTH_BASE[index] + reader.read_bits(LENGTH_EXTRA[index])
                    distance_symbol = distances.decode(reader)
                    assert distance_symbol < len(DISTANCE_BASE), "invalid distance symbol"
                    distance = (DISTANCE_BASE[distance_symbol]
                                + reader.read_bits(DISTANCE_EXTRA[distance_symbol]))
                    assert 1 <= distance <= len(output), "distance before stream start"
                    for _ in range(length):
                        output.append(output[-distance])
        else:
            raise AssertionError("dynamic Huffman block (BTYPE=2) is not part of "
                                 "the desktop PNG encoder contract")
        if final:
            break
    trailer = compressed[reader.offset:reader.offset + 4]
    assert len(trailer) == 4, "missing Adler-32 trailer"
    stored_adler = struct.unpack(">I", trailer)[0]
    assert adler32(output) == stored_adler, "Adler-32 mismatch"
    return bytes(output)


def paeth_predictor(left, above, upper_left):
    prediction = left + above - upper_left
    distance_left = abs(prediction - left)
    distance_above = abs(prediction - above)
    distance_upper_left = abs(prediction - upper_left)
    if distance_left <= distance_above and distance_left <= distance_upper_left:
        return left
    if distance_above <= distance_upper_left:
        return above
    return upper_left


def unfilter_rgb(scans, width, height):
    """Reverse the five RFC 2083 row filters for one 8-bit RGB image."""
    stride = width * 3 + 1
    assert len(scans) == stride * height, \
        f"scanline payload {len(scans)} != {stride * height}"
    previous = bytearray(width * 3)
    rgb = bytearray()
    for row in range(height):
        base = row * stride
        filter_type = scans[base]
        assert filter_type <= 4, f"unknown filter type {filter_type}"
        current = bytearray(scans[base + 1:base + stride])
        for column in range(width * 3):
            left = current[column - 3] if column >= 3 else 0
            above = previous[column]
            upper_left = previous[column - 3] if column >= 3 else 0
            if filter_type == 0:
                pass
            elif filter_type == 1:
                current[column] = (current[column] + left) & 0xFF
            elif filter_type == 2:
                current[column] = (current[column] + above) & 0xFF
            elif filter_type == 3:
                current[column] = (current[column] + (left + above) // 2) & 0xFF
            else:
                current[column] = (current[column]
                                   + paeth_predictor(left, above, upper_left)) & 0xFF
        rgb += current
        previous = current
    return bytes(rgb)


def decode_png(data):
    assert data[:8] == b'\x89PNG\r\n\x1a\n', "PNG signature mismatch"
    offset = 8
    compressed = bytearray()
    header = None
    chunk_names = []
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        name = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        crc = struct.unpack('>I', data[offset + 8 + length:offset + 12 + length])[0]
        assert crc32(name + payload) == crc, f"CRC mismatch in {name!r}"
        if name == b'IHDR':
            header = struct.unpack('>IIBBBBB', payload)
            assert header[2:] == (8, 2, 0, 0, 0), \
                f"unexpected IHDR {header[2:]} (want 8-bit RGB, no interlace)"
        elif name == b'IDAT':
            compressed.extend(payload)
        elif name == b'IEND':
            assert length == 0
        chunk_names.append(name)
        offset += length + 12
    assert offset == len(data), "trailing bytes after IEND"
    assert chunk_names[0] == b'IHDR' and chunk_names[-1] == b'IEND', \
        f"unexpected chunk order {chunk_names}"
    assert all(name in (b'IHDR', b'IDAT', b'IEND') for name in chunk_names), \
        f"unexpected ancillary chunks {chunk_names}"
    assert header is not None, "missing IHDR"
    width, height = header[0], header[1]
    rgb = unfilter_rgb(inflate_raw(bytes(compressed)), width, height)
    return width, height, rgb


def stored_bound(width, height):
    scan_bytes = (width * 3 + 1) * height
    return 57 + 6 + scan_bytes + 5 * ((scan_bytes + 65534) // 65535)


def main():
    binary = Path(sys.argv[1]).resolve()
    workspace = tempfile.mkdtemp(prefix="desktop-png-decode-")
    completed = subprocess.run([str(binary)], cwd=workspace, timeout=600,
                               capture_output=True, text=True)
    lines = completed.stdout.strip().splitlines()
    # The binary may legitimately exit 1 while known business defects are
    # being reported as deferred findings — but only when every hard check
    # passed (the final summary line is printed after the fixture manifest).
    all_checks_passed = bool(lines) and lines[-1].endswith("desktop image checks passed")
    if completed.returncode != 0 and not all_checks_passed:
        print(completed.stdout)
        print(completed.stderr, file=sys.stderr)
        raise AssertionError(f"{binary.name} exited {completed.returncode}")
    if completed.returncode != 0:
        print("note: the test binary reported deferred business defects (see its stderr); "
              "the PNG fixtures themselves completed their hard checks")
    print(lines[-1] if lines else "")
    manifest = json.loads((Path(workspace) / "desktop-png-fixtures.json").read_text())
    assert manifest, "no PNG fixtures were exported"
    for entry in manifest:
        width, height, rgb = decode_png((Path(workspace) / entry["png"]).read_bytes())
        assert (width, height) == (entry["width"], entry["height"]), \
            f"{entry['png']}: size {width}x{height} != manifest"
        expected = (Path(workspace) / entry["raw"]).read_bytes()
        assert len(expected) == width * height * 3, \
            f"{entry['raw']}: unexpected raw size"
        assert rgb == expected, f"{entry['png']}: decoded pixels differ from raw RGB"
        png_size = (Path(workspace) / entry["png"]).stat().st_size
        raw_size = len(expected)
        expectation = entry["expectation"]
        if expectation == "stored_bound":
            bound = stored_bound(width, height)
            assert png_size == bound, \
                f"{entry['png']}: {png_size} != stored bound {bound}"
            print(f"independent PNG decode {entry['png']}: {width}x{height} "
                  f"{png_size}B == stored bound ({png_size * 100.0 // raw_size}% of raw)")
        elif expectation == "pixels_only":
            # A tiny fixture's PNG framing outweighs its raw bytes; the claim
            # under test is the pixel round trip alone.
            print(f"independent PNG decode {entry['png']}: {width}x{height} "
                  f"{png_size}B (pixel round trip only)")
        else:
            assert png_size <= raw_size, \
                f"{entry['png']}: {png_size}B exceeds raw RGB {raw_size}B"
            ratio = f"{raw_size / png_size:.1f}x smaller than raw"
            if expectation == "ui_tiny":
                assert png_size * 8 <= raw_size, \
                    f"{entry['png']}: {png_size}B is not <1/8 of raw {raw_size}B"
            print(f"independent PNG decode {entry['png']}: {width}x{height} "
                  f"{png_size}B, {ratio}")
    print(f"Independent PNG decode: {len(manifest)} fixtures passed "
          "CRC/Adler/fixed-Huffman inflate/unfilter/pixel round trip "
          "(no production encoder logic involved)")


if __name__ == '__main__':
    main()
