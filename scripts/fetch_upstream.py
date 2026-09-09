#!/usr/bin/env python3
"""Fetch only pinned TT-UMD and the small Linux ARM64 simulator asset.

No submodules or full Metal checkout. Does not alter an existing checkout.
Run inside the remote shared build lock when on the coordinated server.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "provenance.json").read_text())


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for item in MANIFEST["umd"]["retained_source"] + [MANIFEST["descriptor"]]:
        path = ROOT / item["local_path"]
        if sha256(path) != item["sha256"]:
            raise RuntimeError(f"Retained source hash mismatch: {path.name}")

    umd = ROOT / "third_party/tt-umd"
    if not umd.exists():
        subprocess.run(["git", "clone", "--filter=blob:none", "--no-checkout",
                        MANIFEST["umd"]["repository"], str(umd)], check=True)
        subprocess.run(["git", "-C", str(umd), "checkout", "--detach",
                        MANIFEST["umd"]["commit"]], check=True)
    commit = subprocess.check_output(["git", "-C", str(umd), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(umd), "status", "--porcelain"], text=True)
    if commit != MANIFEST["umd"]["commit"] or dirty:
        raise RuntimeError("Existing UMD checkout does not match clean pin; no files changed")

    destination = ROOT / "third_party/ttsim"
    destination.mkdir(exist_ok=True)
    library = destination / MANIFEST["ttsim"]["asset"]
    if not library.exists():
        temporary = library.with_suffix(".download")
        try:
            with urllib.request.urlopen(MANIFEST["ttsim"]["asset_url"], timeout=60) as response:
                with temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
            if sha256(temporary) != MANIFEST["ttsim"]["asset_sha256"]:
                raise RuntimeError("Simulator download hash mismatch")
            temporary.replace(library)
        finally:
            temporary.unlink(missing_ok=True)
    if sha256(library) != MANIFEST["ttsim"]["asset_sha256"]:
        raise RuntimeError("Existing simulator hash mismatch; no replacement attempted")
    descriptor = destination / "soc_descriptor.yaml"
    original = ROOT / MANIFEST["descriptor"]["local_path"]
    if descriptor.exists() and sha256(descriptor) != MANIFEST["descriptor"]["sha256"]:
        raise RuntimeError("Existing descriptor mismatch; no replacement attempted")
    if not descriptor.exists():
        shutil.copyfile(original, descriptor)
    print(f"UMD pinned {commit}; ttsim verified {library.stat().st_size} bytes")
    print("Fetch complete; this is not simulator execution evidence")


if __name__ == "__main__":
    main()
