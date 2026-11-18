#!/usr/bin/env python3
"""Publish an immutable Forge site image, then switch the tested public manifest."""
import argparse
import base64
import hashlib
import json
import mimetypes
import os
import re
import subprocess
import tempfile
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from google.api_core.exceptions import NotFound, PreconditionFailed
from google.cloud import storage

BUCKET = "forge-lang-514802-frontend"
PREVIEW = "https://gcp-preview.forge-lang.org"
LIVE = "https://forge-lang.org"


def read_pointer(blob):
    try:
        blob.reload()
    except NotFound:
        return 0, None
    generation = int(blob.generation)
    return generation, blob.download_as_bytes(if_generation_match=generation)


def write_pointer(blob, data, generation):
    blob.cache_control = "public,max-age=15,must-revalidate"
    blob.upload_from_string(data, content_type="application/json", if_generation_match=generation)
    return int(blob.generation)


def publish_files(bucket, directory):
    existing = {item.name: item.md5_hash for prefix in ("assets/", "origin-cd/blobs/")
                for item in bucket.list_blobs(prefix=prefix)}
    files = sorted(path for path in directory.rglob("*") if path.is_file())
    mapped = {}

    def publish(path):
        relative = path.relative_to(directory).as_posix()
        if relative in ("50x.html", "install.sh") or relative.startswith("."):
            return
        data = path.read_bytes()
        if relative.startswith("assets/"):
            if not re.fullmatch(r"assets/[a-zA-Z0-9_.-]+", relative):
                raise ValueError("Unexpected asset name: " + relative)
            name = relative
        else:
            extension = path.suffix.lstrip(".")
            if not re.fullmatch("[A-Za-z0-9]+", extension):
                raise ValueError("Unexpected static file extension: " + relative)
            name = "origin-cd/blobs/" + hashlib.sha256(data).hexdigest() + "." + extension
        expected = base64.b64encode(hashlib.md5(data).digest()).decode()
        actual = existing.get(name)
        if actual is None:
            blob = bucket.blob(name)
            blob.cache_control = "public,max-age=31536000,immutable"
            try:
                mime = mimetypes.guess_type(relative)[0] or "application/octet-stream"
                blob.upload_from_string(data, content_type=mime, if_generation_match=0)
            except PreconditionFailed:
                blob.reload()
            actual = blob.md5_hash
        if actual != expected:
            raise RuntimeError("Immutable object checksum mismatch: " + name)
        return ("/" + relative, name) if not relative.startswith("assets/") else None

    with ThreadPoolExecutor(max_workers=8) as pool:
        for entry in pool.map(publish, files):
            if entry:
                mapped[entry[0]] = entry[1]
    for required in ("/index.html", "/logo.webp", "/playground/runner.html", "/playground/compiler.wasm"):
        if required not in mapped:
            raise RuntimeError("Incomplete site image: " + required)
    return mapped


def verify(host, digest, directory):
    for path in ("/", "/play", "/learn", "/logo.webp", "/playground/runner.html", "/playground/compiler.wasm"):
        local = directory / ("index.html" if path in ("/", "/play", "/learn") else path.lstrip("/"))
        for attempt in range(12):
            with urllib.request.urlopen(host + path, timeout=30) as response:
                if response.headers.get("X-Forge-Site-Revision") == digest:
                    if response.read() != local.read_bytes():
                        raise RuntimeError("Public content differs from the image: " + path)
                    csp = response.headers.get("Content-Security-Policy", "")
                    required = "sandbox allow-scripts" if path.endswith("/runner.html") else "default-src 'self'"
                    if required not in csp:
                        raise RuntimeError("Missing expected CSP: " + path)
                    break
            time.sleep(3)
        else:
            raise RuntimeError("Public Worker did not serve the expected image on " + host + path)


def is_main(commit):
    result = subprocess.check_output(["git", "ls-remote", "--exit-code", "origin", "refs/heads/main"],
                                     env={**os.environ, "GIT_MASTER": "1"}, text=True)
    return result.split()[0] == commit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("commit")
    args = parser.parse_args()
    digest = args.image.split("@")[-1]
    if not re.fullmatch(r"sha256:[a-f0-9]{64}", digest):
        parser.error("supply an image pinned by digest")
    bucket = storage.Client().bucket(BUCKET)
    live = bucket.blob("origin-cd/current.json")
    old_generation, old = read_pointer(live)
    if old and json.loads(old)["image"] == digest:
        print("Public manifest already serves " + digest)
        return
    with tempfile.TemporaryDirectory(prefix="forge-site-") as temporary:
        directory = Path(temporary)
        subprocess.run(["docker", "pull", args.image], check=True)
        container = subprocess.check_output(["docker", "create", args.image], text=True).strip()
        try:
            subprocess.run(["docker", "cp", container + ":/usr/share/nginx/html/.", str(directory)], check=True)
        finally:
            subprocess.run(["docker", "rm", container], check=True)
        manifest = {"image": digest, "files": publish_files(bucket, directory)}
        data = json.dumps(manifest, separators=(",", ":"), sort_keys=True).encode()
        preview = bucket.blob("origin-cd/preview.json")
        generation, _ = read_pointer(preview)
        write_pointer(preview, data, generation)
        verify(PREVIEW, digest, directory)
        if not is_main(args.commit):
            print("A newer main commit superseded this release")
            return
        new_generation = write_pointer(live, data, old_generation)
        try:
            verify(LIVE, digest, directory)
        except Exception:
            if old is None:
                live.delete(if_generation_match=new_generation)
            else:
                write_pointer(live, old, new_generation)
            raise
        print("Verified public Forge release " + digest)


if __name__ == "__main__":
    main()
