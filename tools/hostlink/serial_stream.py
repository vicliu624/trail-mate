"""Raw duplex PySerial transport; framing and business remain in the JS client."""
import json
import sys
import threading

import serial

output_lock = threading.Lock()
stop = threading.Event()


def emit(value):
    with output_lock:
        print(json.dumps(value), flush=True)


def main():
    port = serial.Serial(port=None, baudrate=int(sys.argv[2]), timeout=0.1, write_timeout=2)
    port.port = sys.argv[1]
    # Set both in the initial DCB when opening Windows native USB CDC. Some
    # drivers do not publish the later separate EscapeCommFunction updates as
    # one connected line-state transition. HostlinkConnection asserts both too.
    port.dtr = True
    port.rts = True
    reader = None
    try:
        port.open()

        def read():
            try:
                while not stop.is_set():
                    data = port.read(512)
                    if data:
                        emit({"data": data.hex()})
            except Exception as error:
                if not stop.is_set():
                    emit({"error": str(error)})
                    stop.set()

        reader = threading.Thread(target=read, daemon=True)
        reader.start()
        emit({"ready": True})
        for line in sys.stdin:
            if len(line) > 4096:
                raise ValueError("Serial command exceeds transport bound")
            command = json.loads(line)
            if "write" in command:
                data = bytes.fromhex(command["write"])
                if len(data) > 522 or port.write(data) != len(data):
                    raise ValueError("Invalid or incomplete serial write")
            elif "signals" in command:
                port.dtr = bool(command["signals"]["dataTerminalReady"])
                port.rts = bool(command["signals"]["requestToSend"])
            else:
                raise ValueError("Unknown serial stream command")
            emit({"id": command["id"], "ok": True})
    except Exception as error:
        emit({"error": type(error).__name__ + ": " + str(error)})
        return 1
    finally:
        stop.set()
        if reader:
            reader.join(timeout=1)
        if port.is_open:
            port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
