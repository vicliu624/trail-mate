"""Check the native incremental stamp against official LXMF's full workblock."""
import json
import subprocess
import sys

import LXMF
import LXMF.LXStamper as stamper

assert LXMF.__version__ == "1.2.1"
result = subprocess.run([sys.argv[1]], check=True, text=True, capture_output=True, timeout=15)
data = json.loads(result.stdout)
workblock = stamper.stamp_workblock(bytes.fromhex(data["transient"]), expand_rounds=stamper.WORKBLOCK_EXPAND_ROUNDS_PN)
assert stamper.stamp_valid(bytes.fromhex(data["stamp"]), data["cost"], workblock)
print("Official LXMF validated the native incremental propagation stamp using all 1000 expansion rounds")
