"""Independent HostLink TCP/exec proof test, usable on ARM without Node packages.

All application data and generated identities belong to temporary fixture roots.
Only public verification material is written to files; signed messages use memfd.
"""
import os
import pathlib
import secrets
import select
import socket
import subprocess
import sys
import tempfile
import time
from linux_tms_application_test import Peer, decode, encode as base_encode


def encode(value):
    if isinstance(value, bool):
        return b'\xc3' if value else b'\xc2'
    if isinstance(value, list):
        assert len(value) < 16
        return bytes([0x90 + len(value)]) + b''.join(map(encode, value))
    return base_encode(value)


class Session(Peer):
    def call(self, operation, body):
        request = secrets.token_bytes(16)
        self.send(0x20, encode([1, self.session, request, operation, body]))
        version, session, correlation, status, response = decode(self.receive(0x21))
        assert version == 1 and session == self.session and correlation == request
        return status, response

    def drop_restore(self, body, operation=5):
        self.send(0x20, encode([1, self.session, secrets.token_bytes(16), operation, body]))
        # Keep reads open so earlier ACKs cannot cause an unread-data TCP reset.
        # No commit response is consumed; acceptance requires fresh post-exec proof.
        self.socket.shutdown(socket.SHUT_WR)


class Fixture:
    def __init__(self, binary, root, mode, application=None, profile=None, runtime='local'):
        assert runtime in ('local', 'mesh')
        self.application = application
        self.root = root
        self.stamp = None
        if application:
            bootstrap = Fixture(binary, root, mode)
            bootstrap.endpoint()
            bootstrap.close()
            lines = pathlib.Path(profile).read_text().splitlines()
            lines = [line for line in lines if not line.startswith(('runtime_mode=', 'radio_driver=', 'keyboard_pwm_', 'hardware_keyboard='))]
            selected = root / 'device.conf'
            selected.write_text('\n'.join(lines) + f'\nruntime_mode={runtime}\nradio_driver=none\nhardware_keyboard=false\n')
            env = dict(os.environ)
            for key in ('SETTINGS', 'CACHE', 'SD', 'STATE'):
                env[f'TRAIL_MATE_{key}_ROOT'] = str(root)
            env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', TRAIL_MATE_LORA_DISABLE='1',
                       TRAIL_MATE_RUNTIME_MODE=runtime, TRAIL_MATE_HOSTLINK_BIND='127.0.0.1',
                       TRAIL_MATE_GPS_DEVICE='/dev/trailmate-no-test-gps', TRAIL_MATE_DESKTOP_NOTIFICATIONS='0')
            with (root / 'application.log').open('wb') as log:
                self.process = subprocess.Popen([application, '--profile', str(selected), '--quit-after-ms', '30000'],
                                                env=env, stdin=subprocess.PIPE, stdout=log, stderr=log)
        else:
            self.process = subprocess.Popen([binary, str(root), mode], stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        self.buffer = b''

    def endpoint(self):
        deadline = time.monotonic() + 15
        if self.application:
            endpoint = self.root / 'hostlink/endpoint.txt'
            while time.monotonic() < deadline:
                assert self.process.poll() is None, 'formal application exited before endpoint'
                try:
                    status = endpoint.stat()
                    stamp = status.st_ino, status.st_mtime_ns
                    value = endpoint.read_text().strip()
                    if value and stamp != self.stamp:
                        self.stamp = stamp
                        return value
                except FileNotFoundError:
                    pass
                time.sleep(.02)
            raise AssertionError('formal application did not replace its endpoint across exec')
        while True:
            while b'\n' in self.buffer:
                line, self.buffer = self.buffer.split(b'\n', 1)
                if line.startswith(b'ENDPOINT '):
                    return line[9:].decode()
            assert time.monotonic() < deadline, 'fixture endpoint timed out'
            ready, _, _ = select.select([self.process.stdout], [], [], .1)
            if ready:
                data = os.read(self.process.stdout.fileno(), 4096)
                assert data, 'fixture exited before endpoint'
                self.buffer += data

    def close(self):
        if self.process.poll() is None:
            if self.application:
                self.process.terminate() # Only this owned isolated test process.
                result = self.process.wait(timeout=5)
                assert result in (0, -15)
                return
            try:
                self.process.stdin.write(b'quit\n')
                self.process.stdin.flush()
            except BrokenPipeError:
                pass
        try:
            result = self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        assert result == 0, ('fixture exit', result, self.process.stderr.read()[-2000:] if self.process.stderr else b'')


def verify(public_key, signature, message, directory, executable):
    assert len(public_key) == 32 and len(signature) == 64
    (directory / 'public.der').write_bytes(bytes.fromhex('302a300506032b6570032100') + public_key)
    (directory / 'signature.bin').write_bytes(signature)
    fd = os.memfd_create('trailmate-identity-fixture-message', os.MFD_CLOEXEC)
    try:
        os.write(fd, message)
        os.lseek(fd, 0, 0)
        result = subprocess.run([executable, 'pkeyutl', '-verify', '-rawin', '-pubin',
                                 '-keyform', 'DER', '-inkey', str(directory / 'public.der'),
                                 '-sigfile', str(directory / 'signature.bin'), '-in', f'/proc/self/fd/{fd}'],
                                pass_fds=(fd,), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode == 0, 'independent Ed25519 verification failed'
    finally:
        os.lseek(fd, 0, 0)
        os.write(fd, b'\0' * len(message))
        os.close(fd)


def author(session, directory, openssl):
    status, identity = session.call(1, [])
    assert status == 0 and len(identity) == 4
    public_key, identity_hash, delivery, generation = identity
    assert len(public_key) == 64 and len(identity_hash) == len(delivery) == 16 and generation > 0
    challenge = secrets.token_bytes(32)
    status, proof = session.call(2, [challenge, generation])
    assert status == 0 and proof[:4] == identity and len(proof[4]) == 64
    message = b'trailmate.hostlink.author/v1\0' + encode([1, session.session, challenge, public_key, generation])
    verify(public_key[32:], proof[4], message, directory, openssl)
    return identity


def main():
    binary, openssl = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'openssl'
    application = sys.argv[3] if len(sys.argv) > 3 else None
    profile = sys.argv[4] if len(sys.argv) > 4 else None
    for mode in (('seed',) if application else ('seed', 'expanded', 'write-failure')):
        with tempfile.TemporaryDirectory(prefix='trailmate-arm-identity-') as temporary:
            root = pathlib.Path(temporary)
            source = target = a = b = None
            try:
                source = Fixture(binary, root / 'source', 'expanded' if mode == 'expanded' else 'seed', application, profile)
                target = Fixture(binary, root / 'target', 'write-failure' if mode == 'write-failure' else 'seed', application, profile)
                a = Session(source.endpoint(), secrets.token_bytes(16))
                b = Session(target.endpoint(), secrets.token_bytes(16))
                source_author = author(a, root, openssl)
                before = author(b, root, openssl)
                assert before[0] != source_author[0]
                status, formats = b.call(6, [])
                assert status == 0 and formats == [0, 1]
                status, backup = a.call(4, [source_author[3], True])
                assert status == 0 and len(backup) == 2
                payload, signature = backup
                verify(source_author[0][32:], signature, b'TrailMate.Identity.Backup.v1\0' + payload, root, openssl)
                assert payload[:2] == b'\x98\x01' and payload[2] & 0xe0 == 0xa0
                type_size = payload[2] & 31
                assert payload[3:3 + type_size] == b'trailmate.identity.backup.v1'
                assert payload[3 + type_size] == (0 if mode == 'expanded' else 1)
                if mode == 'expanded':
                    assert source_author[3] == 3 and before[3] == 1
                request = [before[3], before[0], payload, signature, True]
                assert b.call(5, [*request[:4], False])[0] == 2
                assert b.call(5, [before[3] + 1, *request[1:]])[0] == 9
                b.drop_restore(request)
                replacement = target.endpoint()
                b.close()
                b = Session(replacement, secrets.token_bytes(16))
                after = author(b, root, openssl)
                if mode == 'write-failure':
                    assert after == before, 'failed replacement changed identity'
                else:
                    assert after[:3] == source_author[:3] and after[3] == before[3] + 1
                print('IDENTITY_FORMAL_APP_EXEC_PROOF_PASSED' if application else 'IDENTITY_WIRE_EXEC_PROOF_PASSED', mode, flush=True)
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
