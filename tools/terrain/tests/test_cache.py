import hashlib

import pytest

from terrain.cache import ChecksumError, DownloadCache, NetworkError, OfflineError, Source


def source_for(server, data: bytes, name="dem.tif", **extra) -> Source:
    server.files["/" + name] = data
    return Source("copdem", name, server.base_url + "/" + name, size=len(data),
                  sha256=hashlib.sha256(data).hexdigest(), **extra)


def test_downloads_into_dataset_folder_and_never_again(tmp_path, server):
    data = bytes(range(256)) * 100
    source = source_for(server, data)
    cache = DownloadCache(tmp_path)
    path = cache.fetch(source)
    assert path == tmp_path / "copdem" / "dem.tif"
    assert path.read_bytes() == data
    assert len(server.requests) == 1
    assert cache.fetch(source) == path
    assert len(server.requests) == 1  # a valid file is never downloaded again


def test_resumes_a_stopped_download(tmp_path, server):
    data = bytes(range(256)) * 1000
    source = source_for(server, data)
    server.fail_after = 70_000  # the first response stops early
    cache = DownloadCache(tmp_path)
    with pytest.raises(NetworkError, match="resumed next time"):
        cache.fetch(source)
    part = tmp_path / "copdem" / "dem.tif.part"
    kept = part.stat().st_size
    assert 0 < kept < len(data)
    server.requests.clear()
    assert cache.fetch(source).read_bytes() == data
    assert server.requests == [("/dem.tif", f"bytes={kept}-")]  # only the rest was asked for
    assert not part.exists()


def test_wrong_checksum_is_refused_and_removed(tmp_path, server):
    data = b"hello world" * 50
    source = source_for(server, data)
    bad = Source(source.dataset, source.name, source.url, size=source.size, sha256="0" * 64)
    with pytest.raises(ChecksumError, match="SHA-256"):
        DownloadCache(tmp_path).fetch(bad)
    assert not (tmp_path / "copdem" / "dem.tif").exists()
    assert not (tmp_path / "copdem" / "dem.tif.part").exists()


def test_wrong_size_is_refused(tmp_path, server):
    data = b"abc" * 10
    server.files["/x.bin"] = data
    source = Source("d", "x.bin", server.base_url + "/x.bin", size=999)
    with pytest.raises(ChecksumError, match="expected 999 bytes"):
        DownloadCache(tmp_path).fetch(source)


def test_an_invalid_cached_file_is_downloaded_again(tmp_path, server):
    data = b"fresh" * 100
    source = source_for(server, data)
    path = tmp_path / "copdem" / "dem.tif"
    path.parent.mkdir(parents=True)
    path.write_bytes(b"stale")
    assert DownloadCache(tmp_path).fetch(source).read_bytes() == data


def test_http_errors_and_dead_servers_say_what_to_do(tmp_path, server):
    missing = Source("d", "nope.bin", server.base_url + "/nope.bin")
    with pytest.raises(NetworkError, match="HTTP 404"):
        DownloadCache(tmp_path).fetch(missing)
    unreachable = Source("d", "x.bin", "http://127.0.0.1:1/x.bin")
    with pytest.raises(NetworkError, match="HTTPS_PROXY"):
        DownloadCache(tmp_path).fetch(unreachable)


def test_offline_mode_uses_the_cache_and_fails_fast_otherwise(tmp_path, server):
    data = b"cached" * 20
    source = source_for(server, data)
    DownloadCache(tmp_path).fetch(source)
    offline = DownloadCache(tmp_path, offline=True)
    assert offline.fetch(source).read_bytes() == data
    other = Source("copdem", "other.tif", server.base_url + "/other.tif")
    with pytest.raises(OfflineError, match=r"copdem/other\.tif"):
        offline.require_offline([source, other])
    with pytest.raises(OfflineError):
        offline.fetch(other)
    assert all(path != "/other.tif" for path, _ in server.requests)


def test_names_cannot_escape_the_cache(tmp_path):
    cache = DownloadCache(tmp_path)
    with pytest.raises(Exception, match="plain file or folder name"):
        cache.path_of(Source("..", "x", "http://example.invalid/x"))
    with pytest.raises(Exception, match="plain file or folder name"):
        cache.path_of(Source("d", "a/b", "http://example.invalid/x"))


def test_source_entries_are_checked():
    assert Source.from_json({"dataset": "d", "name": "n", "url": "u", "size": 3}).size == 3
    with pytest.raises(Exception, match="'url'"):
        Source.from_json({"dataset": "d", "name": "n"})
    with pytest.raises(Exception, match="64 lower-case hex"):
        Source.from_json({"dataset": "d", "name": "n", "url": "u", "sha256": "abc"})
