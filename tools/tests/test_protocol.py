"""Protocol unit tests (python3 -m unittest discover tools/tests)."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import scope_proto as sp  # noqa: E402

# Same golden frame as tests/test_crc_protocol.c: PING, seq 7, payload "hi".
GOLDEN_PING = bytes([0xA5, 0x5A, 0x01, 0x07, 0x02, 0x00, ord("h"), ord("i"),
                     0xCE, 0x5E, 0xFD, 0xFB])


class ProtocolTest(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(sp.crc32(b"123456789"), 0xCBF43926)

    def test_golden_frame_matches_firmware(self):
        self.assertEqual(sp.encode(sp.PING, 7, b"hi"), GOLDEN_PING)

    def test_roundtrip_with_garbage_and_split_reads(self):
        stream = b"\x00\xA5\x13" + sp.encode(sp.STATUS, 1, bytes(96)) + b"\xA5" + \
            sp.encode(sp.ACK, 2, b"\x11\x00")
        p = sp.Parser()
        frames = []
        for i in range(0, len(stream), 5):          # arbitrary chunking
            frames += list(p.feed(stream[i:i + 5]))
        self.assertEqual([f.type for f in frames], [sp.STATUS, sp.ACK])
        self.assertEqual(p.crc_errors, 0)

    def test_corruption_rejected(self):
        good = sp.encode(sp.SET_TIMEBASE, 3, b"\x05")
        for i in range(2, len(good)):
            bad = bytearray(good)
            bad[i] ^= 0x10
            self.assertEqual(list(sp.Parser().feed(bytes(bad))), [])

    def test_capture_header_layout(self):
        raw = struct.pack(sp.CAPTURE_HDR_FMT, 1, 4000, 2000, -0.25, 80_000_000, 16,
                          100_000, 0, 0, 1, 0, 2048, 500, 3300.0, 1.0, 0.0, 0xDEADBEEF)
        h = sp.parse_capture_header(raw)
        self.assertEqual(h.sample_rate, 5e6)
        self.assertTrue(h.forced)
        self.assertAlmostEqual(sp.raw_to_volts([4095], h)[0], 3.3, places=4)


if __name__ == "__main__":
    unittest.main()


class FakeSerial:
    """Stands in for pyserial: replies to GET_CAPTURE with a header + chunks."""

    def __init__(self, samples, chunk=500):
        self.samples, self.chunk, self.rx = samples, chunk, bytearray()

    in_waiting = property(lambda self: len(self.rx))

    def reset_input_buffer(self):
        self.rx.clear()

    def read(self, n):
        out, self.rx = bytes(self.rx[:n]), self.rx[n:]
        return out

    def write(self, data):
        f = next(sp.Parser().feed(data))
        assert f.type == sp.GET_CAPTURE
        n = len(self.samples)
        raw = struct.pack(f"<{n}H", *self.samples)
        hdr = struct.pack(sp.CAPTURE_HDR_FMT, 9, n, n // 2, 0.0, 80_000_000, 16, 100_000,
                          0, 0, 0, 0, 2048, self.chunk, 3300.0, 1.0, 0.0, sp.crc32(raw))
        self.rx += b"\x00junk" + sp.encode(sp.LOG, 0, b"hello") + sp.encode(sp.CAPTURE_HDR, f.seq, hdr)
        for off in range(0, n, self.chunk):
            part = self.samples[off:off + self.chunk]
            body = struct.pack("<HIH", 9, off, len(part)) + struct.pack(f"<{len(part)}H", *part)
            self.rx += sp.encode(sp.CAPTURE_DATA, f.seq, body)

    def close(self):
        pass


class DeviceCaptureTest(unittest.TestCase):
    def test_capture_reassembly_and_frequency(self):
        import math
        try:
            import scope
        except SystemExit:           # pyserial missing
            self.skipTest("pyserial not installed")
        fs, f0 = 5e6, 12_345.0
        samples = [int(2048 + 1500 * math.sin(2 * math.pi * f0 * i / fs)) for i in range(4000)]
        dev = scope.Device.__new__(scope.Device)
        dev.ser, dev.parser, dev.seq, dev.pending = FakeSerial(samples), sp.Parser(), 0, []
        hdr, got = dev.capture()
        self.assertEqual(got, samples)
        self.assertEqual(hdr.sample_rate, fs)
        self.assertAlmostEqual(scope.estimate_frequency(got, fs), f0, delta=f0 * 1e-3)
