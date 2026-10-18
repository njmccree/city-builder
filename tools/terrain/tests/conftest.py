import http.server
import os
import threading
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]


def find_mapgen() -> Path | None:
    """citysim_mapgen: $CITYSIM_MAPGEN, else the newest build under build/ (any preset)."""
    override = os.environ.get("CITYSIM_MAPGEN")
    if override:
        return Path(override) if Path(override).is_file() else None
    candidates = [path for pattern in ("citysim_mapgen", "citysim_mapgen.exe")
                  for path in (REPO_ROOT / "build").glob(f"*/sim/tools/mapgen/**/{pattern}")]
    return max(candidates, key=lambda path: path.stat().st_mtime, default=None)


@pytest.fixture(scope="session")
def mapgen() -> Path:
    path = find_mapgen()
    if path is None:
        pytest.skip("citysim_mapgen is not built (build it, or set CITYSIM_MAPGEN)")
    return path


class RangeHandler(http.server.BaseHTTPRequestHandler):
    """Serves ``server.files`` ({url path: bytes}) with Range support; ``server.fail_after`` makes a response
    stop after that many bytes, to test resuming."""

    def log_message(self, *args):  # keep the test output quiet
        pass

    def do_GET(self):
        data = self.server.files.get(self.path)
        self.server.requests.append((self.path, self.headers.get("Range")))
        if data is None:
            self.send_error(404)
            return
        start = 0
        range_header = self.headers.get("Range")
        if range_header and range_header.startswith("bytes="):
            start = int(range_header[6:].split("-")[0])
            if start >= len(data):
                self.send_error(416)
                return
        body = data[start:]
        self.send_response(206 if start else 200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        limit = self.server.fail_after
        if limit is not None and self.server.fail_count == 0:
            self.server.fail_count += 1
            self.wfile.write(body[:limit])
            self.wfile.flush()
            self.close_connection = True
            return
        self.wfile.write(body)


@pytest.fixture
def server():
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), RangeHandler)
    httpd.files = {}
    httpd.requests = []
    httpd.fail_after = None
    httpd.fail_count = 0
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    httpd.base_url = f"http://127.0.0.1:{httpd.server_address[1]}"
    yield httpd
    httpd.shutdown()
    httpd.server_close()
