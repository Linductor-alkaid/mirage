"""Independent PNG RGB8 round-trip for the padded, multi-block fixture."""

from pathlib import Path
import struct
import sys
import zlib


def main():
    data = Path(sys.argv[1]).read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset = 8
    compressed = bytearray()
    chunks = []
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        name = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        crc = struct.unpack('>I', data[offset + 8 + length:offset + 12 + length])[0]
        assert zlib.crc32(name + payload) == crc
        if name == b'IHDR':
            assert struct.unpack('>IIBBBBB', payload) == (300, 100, 8, 2, 0, 0, 0)
        if name == b'IDAT':
            compressed.extend(payload)
        if name == b'IEND':
            assert length == 0
        chunks.append(name)
        offset += length + 12
    assert offset == len(data) and chunks == [b'IHDR', b'IDAT', b'IEND']
    scans = zlib.decompress(compressed)
    assert len(scans) == 901 * 100
    for y in range(100):
        assert scans[y * 901] == 0
        for x in range(300):
            assert scans[y * 901 + 1 + x * 3:y * 901 + 4 + x * 3] == bytes((193, y, x % 256))
    print('Independent PNG decode: CRC/Adler/multiple DEFLATE blocks '
          'and 30,000 padded-row pixels passed')


if __name__ == '__main__':
    main()
