"""List files on the camera and download one into the evidence dir with progress."""
import argparse
import os

import pyinsta360 as pi
from _common import EVIDENCE, check, finish, write_json

ap = argparse.ArgumentParser()
ap.add_argument("--remote", default=None, help="file to download (default: smallest-looking first .insp, else first file)")
ap.add_argument("--serial", default=None)
args = ap.parse_args()

progress = []
with pi.Camera(args.serial) as cam:
    files = cam.files_list()
    print("files:", len(files), files[:10])
    write_json("files.json", files)
    check(len(files) > 0, "files_list() is non-empty (needs media on the card)")
    if files:
        remote = args.remote or next((f for f in files if f.endswith(".insp")), files[0])
        local = EVIDENCE / os.path.basename(remote)
        cam.download_file(remote, str(local), progress=lambda done, total: progress.append((done, total)))
        print("downloaded", remote, "->", local, "progress calls:", len(progress))
        check(local.exists() and local.stat().st_size > 0, f"download wrote a non-empty {local.name}")
        check(len(progress) > 0, "progress callback was invoked")
        write_json("download.json", {"remote": remote, "local": str(local),
                                     "size": local.stat().st_size if local.exists() else None,
                                     "progress_tail": progress[-3:]})
finish()
