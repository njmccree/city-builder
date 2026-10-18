"""Download cache for source datasets: ``~/.cache/citysim/terrain/<dataset>/<file>``.

Rules: a file that is already there and valid is never downloaded again; a download that stops is resumed from
its ``.part`` file; size and checksum are verified when the caller knows them; and every failure says what
went wrong and what to do about it (offline mode, proxy, a wrong checksum).
"""

from __future__ import annotations

import hashlib
import os
import re
from dataclasses import dataclass
from pathlib import Path

import requests

CACHE_ENV = "CITYSIM_TERRAIN_CACHE"
CHUNK_BYTES = 1 << 20  # for hashing
DOWNLOAD_CHUNK_BYTES = 64 * 1024  # small, so what arrived before a connection drop is already on disk
TIMEOUT_S = 60


class CacheError(RuntimeError):
    """A file could not be provided; the message says why and what to try."""


class OfflineError(CacheError):
    """``--offline`` was given and a needed file is not in the cache."""


class NetworkError(CacheError):
    """The download failed for a network reason (no connection, proxy, timeout, HTTP error)."""


class ChecksumError(CacheError):
    """The downloaded file is not the file that was expected."""


@dataclass(frozen=True)
class Source:
    """One file of a dataset, as a map's ``map.json`` lists it."""

    dataset: str
    name: str
    url: str
    size: int | None = None
    sha256: str | None = None

    @staticmethod
    def from_json(entry: dict) -> "Source":
        for key in ("dataset", "name", "url"):
            if not isinstance(entry.get(key), str) or not entry[key]:
                raise CacheError(f"a source needs a text '{key}': {entry!r}")
        size = entry.get("size")
        if size is not None and (not isinstance(size, int) or size < 0):
            raise CacheError(f"source {entry['dataset']}/{entry['name']}: 'size' must be a whole number of bytes")
        sha256 = entry.get("sha256")
        if sha256 is not None and not re.fullmatch(r"[0-9a-f]{64}", str(sha256)):
            raise CacheError(f"source {entry['dataset']}/{entry['name']}: 'sha256' must be 64 lower-case hex digits")
        return Source(entry["dataset"], entry["name"], entry["url"], size, sha256)


def default_cache_dir() -> Path:
    override = os.environ.get(CACHE_ENV)
    return Path(override) if override else Path.home() / ".cache" / "citysim" / "terrain"


def _safe_part(text: str, what: str) -> str:
    if not text or text in {".", ".."} or "/" in text or "\\" in text or ":" in text:
        raise CacheError(f"{what} {text!r} must be a plain file or folder name")
    return text


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


class DownloadCache:
    def __init__(self, root: Path | None = None, *, offline: bool = False, session: requests.Session | None = None):
        self.root = Path(root) if root is not None else default_cache_dir()
        self.offline = offline
        self._session = session or requests.Session()

    def path_of(self, source: Source) -> Path:
        return self.root / _safe_part(source.dataset, "dataset") / _safe_part(source.name, "file name")

    def is_valid(self, source: Source) -> bool:
        """True if the file is cached and matches the size and checksum that are known."""
        path = self.path_of(source)
        if not path.is_file():
            return False
        if source.size is not None and path.stat().st_size != source.size:
            return False
        return source.sha256 is None or sha256_of(path) == source.sha256

    def require_offline(self, sources: list[Source]) -> None:
        """Fails fast, naming every file that is missing, before any work starts."""
        missing = [s for s in sources if not self.is_valid(s)]
        if missing:
            names = ", ".join(f"{s.dataset}/{s.name}" for s in missing)
            raise OfflineError(f"--offline: {len(missing)} needed file(s) are not in the cache {self.root}: {names}. "
                               "Run once without --offline (or copy the files there) first.")

    def fetch(self, source: Source) -> Path:
        """The cached file, downloading it first if it is missing or invalid."""
        path = self.path_of(source)
        if self.is_valid(source):
            return path
        if self.offline:
            raise OfflineError(f"--offline: {source.dataset}/{source.name} is not in the cache {self.root}")
        part = path.with_name(path.name + ".part")
        if path.exists():
            path.unlink()  # present but invalid: start again
        path.parent.mkdir(parents=True, exist_ok=True)
        self._download(source, part)
        if source.size is not None and part.stat().st_size != source.size:
            actual = part.stat().st_size
            part.unlink()
            raise ChecksumError(f"{source.dataset}/{source.name}: expected {source.size} bytes, got {actual}")
        if source.sha256 is not None and sha256_of(part) != source.sha256:
            part.unlink()
            raise ChecksumError(f"{source.dataset}/{source.name}: the download does not match the expected SHA-256 "
                                f"({source.sha256}); the file at {source.url} may have changed")
        part.replace(path)
        return path

    def _download(self, source: Source, part: Path) -> None:
        have = part.stat().st_size if part.exists() else 0
        if source.size is not None and have > source.size:
            part.unlink()
            have = 0
        headers = {"Range": f"bytes={have}-"} if have else {}
        try:
            with self._session.get(source.url, headers=headers, stream=True, timeout=TIMEOUT_S) as response:
                if response.status_code == 416 and have:  # the part is complete or wrong: start again
                    part.unlink()
                    return self._download(source, part)
                if response.status_code not in (200, 206):
                    raise NetworkError(f"{source.url}: the server answered HTTP {response.status_code}")
                resumed = response.status_code == 206 and have > 0
                with part.open("ab" if resumed else "wb") as handle:
                    for chunk in response.iter_content(DOWNLOAD_CHUNK_BYTES):
                        handle.write(chunk)
        except requests.RequestException as error:
            raise NetworkError(
                f"{source.url}: {error.__class__.__name__}: {error}. Check the network and the HTTPS_PROXY / "
                f"HTTP_PROXY settings; the partial download is kept at {part} and is resumed next time.") from None
