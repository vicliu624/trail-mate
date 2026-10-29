"""Independent TCP read checks; fixture responses are a directory contract simulation."""
import hashlib
import pathlib
import secrets
import subprocess
import sys
import tempfile
import time
from linux_identity_restore_wire_test import Fixture, verify
from linux_track_snapshots_wire_test import Session, decode
from linux_track_signing_wire_test import encode


class ReadSession(Session):
    def any_status(self, operation, body):
        correlation = secrets.token_bytes(16)
        self.send(0x20, encode([1, self.session, correlation, operation, body]))
        version, session, request, status, result = decode(self.receive(0x21))
        assert version == 1 and session == self.session and request == correlation
        return status, result


def read(client, directory, epoch, generation, operation, parameters):
    request = secrets.token_bytes(16)
    assert client.call(91, [request, directory, epoch, generation, operation, parameters]) == [request, operation]
    deadline = time.monotonic() + 10
    while True:
        status, metadata = client.any_status(92, [request])
        if status == 0:
            break
        assert status == 5 and time.monotonic() < deadline
        time.sleep(.01)
    assert metadata[:3] == [request, operation, 200]
    total, digest = metadata[3:]
    content = b''
    for offset in range(0, total, 320):
        length = min(320, total - offset)
        chunk = client.call(93, [request, offset, length])
        assert chunk[:3] == [digest, total, offset] and len(chunk[3]) == length
        content += chunk[3]
    assert hashlib.sha256(content).digest() == digest
    response = decode(content)
    assert response[:5] == [1, 1, operation, request, 200]
    assert client.call(94, [request]) == []
    client.call(92, [request], 10)
    return response[5]


def main():
    binary, image, openssl = sys.argv[1:4]
    with tempfile.TemporaryDirectory(prefix='trailmate-read-wire-') as temporary:
        root = pathlib.Path(temporary)
        process = subprocess.Popen([binary, str(root), image], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        client = None
        try:
            endpoint = process.stdout.readline().decode().strip()
            assert endpoint.startswith('tcp://')
            client = ReadSession(endpoint, secrets.token_bytes(16))
            author = client.call(1, [])
            assert all(op in client.call(0, [])[4] for op in (91, 92, 93, 94))
            directory_key, delivery, epoch, signed = decode((root / 'read-fixture.cmp').read_bytes())
            raw, signature = decode(signed)
            manifest = decode(raw)
            object_id = hashlib.sha256(b'TrailMate.Track.ID.v1\0' + manifest[1] + manifest[2]).digest()
            revision = hashlib.sha256(b'TrailMate.Track.Revision.v1\0' + raw).digest()
            verify(manifest[1][32:], signature, b'TrailMate.Track.Manifest.v1\0' + raw, root, openssl)
            assert read(client, delivery, epoch, author[3], 0, [])[2] == directory_key
            assert read(client, delivery, epoch, author[3], 2, [object_id, revision]) == [[raw, signature], 1]
            digest, total = manifest[13][:2]
            assert total <= 1024
            response = read(client, delivery, epoch, author[3], 8, [digest, total, 0, total])
            assert response[:3] == [digest, total, 0] and hashlib.sha256(response[3]).digest() == digest
            assert not list((root / 'trailmate/track-read').iterdir())
            print('TRACK_READ_ARM_TCP_PASSED', flush=True)
        finally:
            if client:
                client.close()
            process.stdin.write(b'quit\n')
            process.stdin.flush()
            assert process.wait(timeout=5) == 0, process.stderr.read().decode(errors='replace')[-1500:]
    if len(sys.argv) > 4:
        identity_binary, application, profile = sys.argv[4:7]
        with tempfile.TemporaryDirectory(prefix='trailmate-read-formal-app-') as temporary:
            fixture = client = None
            try:
                fixture = Fixture(identity_binary, pathlib.Path(temporary), 'seed', application, profile, runtime='mesh')
                client = ReadSession(fixture.endpoint(), secrets.token_bytes(16))
                deadline = time.monotonic() + 10
                while not all(op in client.call(0, [])[4] for op in (91, 92, 93, 94)):
                    assert time.monotonic() < deadline
                    time.sleep(.02)
                author = client.call(1, [])
                client.call(89, [0])
                request, delivery, epoch = (secrets.token_bytes(16) for _ in range(3))
                client.call(91, [request, delivery, epoch, author[3], 0, []], 4)
                client.call(91, [request, delivery, epoch, author[3] + 1, 0, []], 9)
                client.call(92, [request], 10)
                client.call(93, [request, 0, 0], 3)
                client.call(94, [request], 10)
                assert client.call(1, []) == author
                print('TRACK_READ_FORMAL_APP_GATES_PASSED', flush=True)
            finally:
                if client:
                    client.close()
                if fixture:
                    fixture.close()


if __name__ == '__main__':
    main()
