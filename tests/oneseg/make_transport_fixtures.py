"""Generate explicitly synthetic TS fixtures; no RF, capture or device access."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def make(name, second=False, remap=False, version=0, audio=True, offset=0):
    command = ["ffmpeg", "-nostdin", "-v", "error", "-y",
               "-f", "lavfi", "-i", "color=c=red:size=320x240:rate=15"]
    if audio:
        command += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000"]
    if second:
        command += ["-f", "lavfi", "-i", "color=c=blue:size=320x240:rate=15",
                    "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000"]
    command += ["-map", "0:v"]
    if audio:
        command += ["-map", "1:a"]
    if second:
        command += ["-map", "2:v", "-map", "3:a"]
    command += ["-t", "12", "-c:v", "libx264", "-preset", "ultrafast",
                "-threads:v", "1", "-g", "15", "-pix_fmt", "yuv420p",
                "-c:a", "aac", "-b:a", "32k", "-streamid", "0:256"]
    if audio:
        command += ["-streamid", "1:257"]
    command += ["-program", "program_num=8:st=0" + (":st=1" if audio else "")]
    if second:
        command += ["-streamid", "2:" + str(768 if remap else 512),
                    "-streamid", "3:" + str(769 if remap else 513),
                    "-program", "program_num=9:st=2:st=3"]
    command += ["-metadata", "service_name=TEST FIXTURE - NO RF",
                "-metadata:p:0", "service_name=TEST FIXTURE - NO RF 8",
                "-mpegts_pmt_start_pid", "8136", "-tables_version", str(version),
                "-muxrate", "500000", "-output_ts_offset", str(offset),
                "-f", "mpegts"]
    if second:
        command += ["-metadata:p:1", "service_name=TEST FIXTURE - NO RF 9"]
    command += [str(args.output / (name + ".ts"))]
    subprocess.run(command, check=True)


make("a", second=True)
make("b", second=True, remap=True, version=1)
make("c", version=2)
make("video", audio=False)
make("jump", audio=False, offset=10)
(args.output / "phase").write_text("a\n", encoding="ascii")
