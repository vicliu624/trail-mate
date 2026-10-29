"""Independent TCP confirmation/import against ARM fixtures or the installed app."""
import hashlib
import pathlib
import secrets
import shutil
import subprocess
import sys
import tempfile
import time
from linux_identity_restore_wire_test import Fixture
from linux_track_snapshots_wire_test import Session
from linux_track_signing_wire_test import encode, upload, prepared


def main():
    binary, openssl = sys.argv[1:3]
    application = sys.argv[3] if len(sys.argv) > 3 else None
    profile = sys.argv[4] if len(sys.argv) > 4 else None
    for imported in (False, True):
        with tempfile.TemporaryDirectory(prefix='trailmate-ip-confirmation-wire-') as temporary:
            root = pathlib.Path(temporary)
            fixture = client = None
            try:
                fixture = Fixture(binary, root, 'seed', application, profile)
                endpoint = fixture.endpoint()
                client = Session(endpoint, secrets.token_bytes(16))
                author = client.call(1, [])
                assert all(op in client.call(0, [])[4] for op in (96, 97, 98))
                kml = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Document><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></Document></kml>'
                digest = hashlib.sha256(kml).digest()
                nonce, operation, request_id, epoch = (secrets.token_bytes(16) for _ in range(4))
                manifest = encode([1, author[0], nonce, 0, 1, None, 0, 100, 100, 'IP confirmation', '', [10000000, 20000000, 10000000, 20000000], [0, None, None, 1], [digest, len(kml), 'application/vnd.google-earth.kml+xml', 1]])
                binding = [operation, upload(client, 3, kml), upload(client, 4, manifest), author[3], None]
                result = prepared(client, 96, binding)
                client.close()
                client = Session(endpoint, secrets.token_bytes(16))
                object_root = root / 'trailmate/tracks' / result[2].hex()
                if imported:
                    assert object_root.parent == root / 'trailmate/tracks'
                    shutil.rmtree(object_root)  # This fixture's temporary object only.
                private = root / 'test-directory.pem'
                subprocess.run([openssl, 'genpkey', '-algorithm', 'ED25519', '-out', str(private)], check=True, capture_output=True)
                signing_public = subprocess.run([openssl, 'pkey', '-in', str(private), '-pubout', '-outform', 'DER'], check=True, capture_output=True).stdout[-32:]
                encryption = root / 'test-encryption.pem'
                subprocess.run([openssl, 'genpkey', '-algorithm', 'X25519', '-out', str(encryption)], check=True, capture_output=True)
                encryption_public = subprocess.run([openssl, 'pkey', '-in', str(encryption), '-pubout', '-outform', 'DER'], check=True, capture_output=True).stdout[-32:]
                directory = encryption_public + signing_public
                identity_hash = hashlib.sha256(directory).digest()[:16]
                delivery = hashlib.sha256(hashlib.sha256(b'lxmf.delivery').digest()[:10] + identity_hash).digest()[:16]
                raw = encode([1, request_id, result[2], result[3], 1, 0, digest, len(kml), directory, delivery, epoch, 1, 100])
                message = root / 'receipt-message.bin'
                message.write_bytes(b'TrailMate.Track.Receipt.v1\0' + raw)
                signature = subprocess.run([openssl, 'pkeyutl', '-sign', '-rawin', '-inkey', str(private), '-in', str(message)], check=True, capture_output=True).stdout
                receipt = encode([raw, signature])
                manifest_id = upload(client, 4, manifest) if imported else None
                transfer = upload(client, 7, receipt)
                request = [operation, transfer, hashlib.sha256(receipt).digest(), author[3], directory, delivery, epoch, request_id, result[2], result[3], 1]
                if imported:
                    request += [manifest_id, hashlib.sha256(manifest).digest(), result[5]]
                code = 98 if imported else 97
                for attempt in range(2):
                    deadline = time.monotonic() + 20
                    while True:
                        response = client.call(code, request)
                        assert response[:3] == [operation, transfer, author[3]]
                        if response[3] == 1:
                            assert response[4:] == [result[2], result[3], 1, 1]
                            break
                        assert response[3:] == [0, None, None, 0, 0] and time.monotonic() < deadline
                        time.sleep(.02)
                    if attempt == 0:
                        client.close()
                        client = Session(endpoint, secrets.token_bytes(16))
                assert receipt in [path.read_bytes() for path in object_root.iterdir()]
                client.call(37, [transfer, 7, len(receipt), hashlib.sha256(receipt).digest()])
                if imported:
                    client.call(37, [manifest_id, 4, len(manifest), hashlib.sha256(manifest).digest()])
                assert client.call(1, []) == author
                print('TRACK_CONFIRMATION_FORMAL_APP_PASSED' if application else 'TRACK_CONFIRMATION_ARM_WIRE_PASSED', 'import' if imported else 'confirm', flush=True)
            finally:
                if client:
                    client.close()
                if fixture:
                    fixture.close()


if __name__ == '__main__':
    main()
