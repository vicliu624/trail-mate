"""Check the status protocol distinguishes decode, presentation and PCM output."""
import json
import subprocess
import sys

rows = [json.loads(line) for line in subprocess.check_output([sys.argv[1]], text=True).splitlines()]
assert len(rows) == 5
decoded, fresh, stale, starved, stopping = rows
assert decoded['video_frames'] == 100 and decoded['presented_frames'] == 0 and not decoded['presenting']
assert fresh['presenting'] and fresh['audio_output_active'] and fresh['pid'] == 42
assert fresh['iq_pairs'] == 2**64-1
assert not stale['presenting'] and stale['audio_output_active']
assert not starved['presenting'] and not starved['audio_output_active'] and starved['pcm_samples'] > 0
assert not stopping['presenting']
print('Telemetry JSON, full-width counters, presentation freshness, audio starvation and truncation passed')
