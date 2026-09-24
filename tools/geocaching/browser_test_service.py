"""Loopback directory with signed fixture and a compression-sized test record."""
import argparse
import os
from pathlib import Path
import threading
import time
import RNS

from directory_service import DirectoryService, exclusive_state
from directory_store import packed, unpacked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--startup-delay', type=float, default=5)
    args = parser.parse_args()
    args.state.mkdir(parents=True, exist_ok=True)
    config = args.state / 'rns'
    config.mkdir(exist_ok=True)
    (config / 'config').write_text('[reticulum]\nshare_instance = No\nenable_transport = Yes\n'
                                 '[interfaces]\n[[Browser test]]\nenabled = Yes\ntype = TCPServerInterface\n'
                                 f'listen_ip = 127.0.0.1\nlisten_port = {args.port}\n')
    with exclusive_state(args.state / 'service'):
        service = DirectoryService(args.state / 'service', config, 'Browser acceptance directory')
        if os.environ.get('GEOCACHING_NETWORK_DEBUG') == '1':
            RNS.loglevel = RNS.LOG_DEBUG
        fixture = Path(__file__).resolve().parents[2] / 'modules/core_geocaching/tests/fixtures/signed-cache-v1.bin'
        signed = unpacked(fixture.read_bytes())
        source = os.urandom(16)
        assert unpacked(service.store.handle(source, packed([1, 0, 1, os.urandom(16), 8192, [signed]])))[4] == 200
        now = int(time.time())
        record = [1, service.identity.get_public_key(), os.urandom(16), 1, None, 0, 310000000, 1210000000,
                  'Browser Resource <&>', ('<b>plain text</b> &' * 200)[:2048], '&' * 512, 3, 4, 2, now, now]
        raw = packed(record)
        signed = [raw, service.identity.sign(b'trailmate.geocache/sign/v1\0' + raw)]
        assert unpacked(service.store.handle(source, packed([1, 0, 1, os.urandom(16), 8192, [signed]])))[4] == 200
        (args.state / 'large-signed.bin').write_bytes(packed(signed))

        def stop_when_requested():
            while not service.stopped.wait(.1):
                if (args.state / 'stop').exists():
                    service.stopped.set()

        threading.Thread(target=stop_when_requested, daemon=True).start()
        service.run(startup_delay=args.startup_delay, run_seconds=180)


if __name__ == '__main__':
    main()
