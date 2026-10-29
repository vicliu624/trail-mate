"""Isolated directory session acceptance through the actual mesh worker."""
import pathlib
import secrets
import sys
import tempfile
import time
from linux_identity_restore_wire_test import Fixture
from linux_track_snapshots_wire_test import Session


def main():
    binary = sys.argv[1]
    application = sys.argv[2] if len(sys.argv) > 2 else None
    profile = sys.argv[3] if len(sys.argv) > 3 else None
    with tempfile.TemporaryDirectory(prefix='trailmate-arm-directories-') as temporary:
        fixture = client = None
        try:
            fixture = Fixture(binary, pathlib.Path(temporary), 'seed', application, profile, runtime='mesh')
            endpoint = fixture.endpoint()
            client = Session(endpoint, secrets.token_bytes(16))
            deadline = time.monotonic() + 10
            while True:
                operations = client.call(0, [])[4]
                if 89 in operations and 90 in operations:
                    break
                assert time.monotonic() < deadline, 'native directory backend did not register'
                time.sleep(.02)
            author = client.call(1, [])
            assert client.call(89, [0]) == [None, []]
            original = client.session
            client.session = secrets.token_bytes(16)
            other = client.session
            client.call(89, [0], 4)
            client.call(3, []) # An unrelated close cannot retire the owner.
            client.call(89, [0], 4)
            client.session = original
            client.call(3, [])
            client.session = other
            client.call(89, [1], 10)
            assert client.call(89, [0]) == [None, []]
            client.call(90, [bytes(16)], 3)
            client.call(90, [secrets.token_bytes(16)], 5) # Radio and IP are disabled in this fixture.
            client.call(89, [9], 3)
            client.close()
            client = Session(endpoint, secrets.token_bytes(16))
            assert client.call(89, [0]) == [None, []]
            assert client.call(1, []) == author
            print('TRACK_DIRECTORIES_FORMAL_APP_PASSED' if application else 'TRACK_DIRECTORIES_ARM_WIRE_PASSED', flush=True)
        finally:
            if client:
                client.close()
            if fixture:
                fixture.close()


if __name__ == '__main__':
    main()
