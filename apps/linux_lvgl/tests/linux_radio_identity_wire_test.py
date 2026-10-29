"""Independent radio-management TCP/exec client; isolated identities only."""
import pathlib
import secrets
import sys
import tempfile
from linux_identity_restore_wire_test import Fixture, Session, author, verify


def radio(session, generation, kind):
    status, row = session.call(7, [generation, kind])
    assert status == 0 and len(row) == 4 and row[0] == kind
    assert row[1] in (0, 1) and 0 <= row[2] <= 0xffffffff
    assert row[3] is None if not row[1] else isinstance(row[3], bytes) and len(row[3]) == 32
    return row


def main():
    binary, openssl = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'openssl'
    application = sys.argv[3] if len(sys.argv) > 3 else None
    profile = sys.argv[4] if len(sys.argv) > 4 else None
    scenarios = [(1, False), (2, False)] if application else [(1, False), (2, False), (1, True), (2, True)]
    for kind, failed in scenarios:
        with tempfile.TemporaryDirectory(prefix='trailmate-radio-wire-') as temporary:
            root = pathlib.Path(temporary)
            source = target = a = b = None
            try:
                source = Fixture(binary, root / 'source', 'radio-source', application, profile)
                mode = ('radio-fail-mc' if kind == 1 else 'radio-fail-mt') if failed else 'seed'
                target = Fixture(binary, root / 'target', mode, application, profile)
                a = Session(source.endpoint(), secrets.token_bytes(16))
                b = Session(target.endpoint(), secrets.token_bytes(16))
                source_author = author(a, root, openssl)
                before_author = author(b, root, openssl)
                source_radio = radio(a, source_author[3], kind)
                before = radio(b, before_author[3], kind)
                assert source_radio[1] == 1
                assert a.call(8, [source_author[3], kind, False])[0] == 2
                status, backup = a.call(8, [source_author[3], kind, True])
                assert status == 0 and len(backup) == 2
                payload, signature = backup
                verify(source_author[0][32:], signature, b'TrailMate.Radio.Identity.Backup.v1\0' + payload, root, openssl)
                request = [before_author[3], before_author[0], payload, signature, before[2], before[3], True]
                assert b.call(9, [*request[:6], False])[0] == 2
                assert b.call(9, [before_author[3] + 1, *request[1:]])[0] == 9
                assert b.call(9, [*request[:4], before[2] + 1, *request[5:]])[0] == 4
                b.drop_restore(request, 9)
                endpoint = target.endpoint()
                b.close(); b = Session(endpoint, secrets.token_bytes(16))
                after_author = author(b, root, openssl)
                assert after_author == before_author, 'radio replacement changed device author'
                after = radio(b, after_author[3], kind)
                if failed:
                    assert after == before, 'failed replacement changed key or generation'
                else:
                    assert after[1] == 1 and after[2] == before[2] + 1 and after[3] == source_radio[3]
                print('RADIO_FORMAL_APP_EXEC_PROOF_PASSED' if application else 'RADIO_WIRE_EXEC_PROOF_PASSED', kind, 'rollback' if failed else 'restore', flush=True)
            finally:
                if a:
                    a.close()
                if b:
                    b.close()
                if source:
                    source.close()
                if target:
                    target.close()


if __name__ == '__main__':
    main()
