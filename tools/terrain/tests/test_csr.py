import struct

import numpy as np
import pytest

from terrain import csr
from terrain.csr import CsrError, decode_csr, encode_csr, read_csr, read_csr_header, write_csr


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.int16, np.float32])
def test_round_trip_for_every_dtype(tmp_path, dtype):
    rng = np.random.default_rng(7)
    info = np.iinfo(dtype) if np.dtype(dtype).kind in "ui" else None
    data = (rng.integers(info.min, info.max, size=(13, 29), dtype=dtype) if info
            else rng.normal(size=(13, 29)).astype(np.float32))
    write_csr(tmp_path / "a" / "x.csr", data, scale=0.25, offset=-3.0)
    back = read_csr(tmp_path / "a" / "x.csr")
    assert back.data.dtype == np.dtype(dtype)
    assert np.array_equal(back.data, data)
    assert (back.scale, back.offset, back.nodata) == (0.25, -3.0, None)


def test_header_layout_is_the_documented_one():
    data = np.arange(6, dtype=np.int16).reshape(2, 3)
    blob = encode_csr(data, scale=0.25, offset=1.5, nodata=-1)
    assert blob[:4] == b"CSRA"
    assert struct.unpack_from("<H", blob, 4)[0] == 1
    assert struct.unpack_from("<II", blob, 6) == (3, 2)  # width, height
    assert blob[14] == 3 and blob[15] == 1  # i16, no-data flag
    assert struct.unpack_from("<ffd", blob, 16) == (0.25, 1.5, -1.0)
    assert struct.unpack_from("<Q", blob, 32)[0] == 12  # 6 cells x 2 bytes
    compressed = struct.unpack_from("<Q", blob, 40)[0]
    assert len(blob) == 48 + compressed + 8


def test_writing_is_deterministic():
    data = np.arange(100, dtype=np.uint16).reshape(10, 10)
    assert encode_csr(data) == encode_csr(data)


def test_row_zero_is_the_south_edge():
    data = np.zeros((3, 4), dtype=np.uint8)
    data[0, :] = 1  # row 0: the southernmost row
    back = decode_csr(encode_csr(data))
    assert back.data[0].tolist() == [1, 1, 1, 1]
    assert back.data[2].tolist() == [0, 0, 0, 0]


def test_values_apply_scale_offset_and_nodata():
    data = np.array([[0, 4, 8], [-32768, 100, 200]], dtype=np.int16)
    raster = decode_csr(encode_csr(data, scale=0.25, offset=10.0, nodata=-32768))
    values = raster.values()
    assert values[0].tolist() == [10.0, 11.0, 12.0]
    assert np.isnan(values[1, 0])
    assert values[1, 1] == 35.0


def test_header_only_read(tmp_path):
    write_csr(tmp_path / "h.csr", np.zeros((5, 9), dtype=np.float32), scale=2.0, nodata=float("nan"))
    header = read_csr_header(tmp_path / "h.csr")
    assert (header.width, header.height, header.type_name, header.scale) == (9, 5, "f32", 2.0)
    assert np.isnan(header.nodata)


def test_unsupported_dtype_and_shape_are_refused():
    with pytest.raises(ValueError, match="u8, u16, i16 or f32"):
        encode_csr(np.zeros((2, 2), dtype=np.float64))
    with pytest.raises(ValueError, match="2D"):
        encode_csr(np.zeros(4, dtype=np.uint8))
    with pytest.raises(ValueError, match="non-empty"):
        encode_csr(np.zeros((0, 4), dtype=np.uint8))


def test_damaged_files_are_refused_with_a_reason(tmp_path):
    good = encode_csr(np.arange(64, dtype=np.uint8).reshape(8, 8))
    with pytest.raises(CsrError, match="shorter than the 48-byte header"):
        decode_csr(good[:20], "short.csr")
    with pytest.raises(CsrError, match='not "CSRA"'):
        decode_csr(b"XXXX" + good[4:], "magic.csr")
    with pytest.raises(CsrError, match="unsupported .csr format version 2"):
        decode_csr(good[:4] + struct.pack("<H", 2) + good[6:], "version.csr")
    with pytest.raises(CsrError, match="header describes"):
        decode_csr(good[:-3], "truncated.csr")
    flipped = bytearray(good)
    flipped[60] ^= 0xFF
    with pytest.raises(CsrError, match="checksum does not match"):
        decode_csr(bytes(flipped), "flipped.csr")
    with pytest.raises(CsrError, match="cannot read"):
        read_csr(tmp_path / "missing.csr")
