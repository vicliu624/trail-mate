"""Run the Arduino composite adapter with host backends and the real owner FSM.

The adapter is local to an ESP translation unit. Extract its complete class
verbatim rather than duplicating its scheduling implementation in the test.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "platform/esp/arduino_common/src/storage/storage_runtime.cpp"
TEST = Path(__file__).with_name("test_storage_adapter_retry.cpp")


def main():
    source = SOURCE.read_text(encoding="utf-8")
    begin = source.index("class SdMaintenanceAdapter final")
    end = source.index("\nSdMaintenanceAdapter s_adapter", begin)
    with tempfile.TemporaryDirectory(prefix="trailmate-storage-retry-") as temporary:
        directory = Path(temporary)
        (directory / "storage_adapter_under_test.inc").write_text(source[begin:end], encoding="utf-8")
        executable = directory / ("retry.exe" if os.name == "nt" else "retry")
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-UNDEBUG",
            "-I" + str(directory), "-I" + str(ROOT / "platform/esp/common/include"),
            "-I" + str(ROOT / "modules/core_sys/include"), str(TEST), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
        peer_source = (ROOT / "platform/esp/arduino_common/src/chat/infra/store/sd_protocol_peer_repository.cpp").read_text(encoding="utf-8")
        methods = []
        for method in ("beginMaintenance", "stepMaintenance", "stepPersistence"):
            start = peer_source.index("SdProtocolPeerRepository::" + method + "(")
            opening = peer_source.index("{", start)
            depth, end = 1, opening + 1
            while depth:
                depth += (peer_source[end] == "{") - (peer_source[end] == "}")
                end += 1
            methods.append("storage_contracts::StorageOperationResult\n" + peer_source[start:end])
        (directory / "peer_maintenance_under_test.inc").write_text("\n".join(methods), encoding="utf-8")
        peer_test = TEST.with_name("test_peer_persistence_completion.cpp")
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-UNDEBUG",
            "-I" + str(directory), "-I" + str(ROOT / "platform/esp/common/include"),
            "-I" + str(ROOT / "modules/core_sys/include"), str(peer_test), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
