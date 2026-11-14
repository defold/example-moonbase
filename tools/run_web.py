#!/usr/bin/env python3
"""Serve the local wasm-web bundle without opening a browser window."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--bundle", type=Path)
    args = parser.parse_args()
    bundles = sorted((ROOT / "build/web").glob("*/index.html"))
    if args.bundle:
        directory = args.bundle.resolve()
    elif len(bundles) == 1:
        directory = bundles[0].parent
    else:
        parser.error("build wasm-web first, or provide --bundle /path/to/bundle")
    if not (directory / "index.html").is_file():
        parser.error(f"no index.html in {directory}")
    handler = partial(SimpleHTTPRequestHandler, directory=directory)
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        print(f"MoonBase web: http://127.0.0.1:{server.server_port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
