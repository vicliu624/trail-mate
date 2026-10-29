"""Two real application service/worker/chat stores, with loopback MQTT relay."""
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
from linux_mqtt_client_test import packet, read_packet, string


def run(binary, mode):
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    port = listener.getsockname()[1]
    listener.listen(2)
    listener.settimeout(5)
    clients = []
    errors = []
    lock = threading.Lock()
    handshake_ready = threading.Barrier(2)
    started = threading.Barrier(2)
    published = []

    def serve(connection):
        try:
            connection.settimeout(5)
            with connection:
                header, body = read_packet(connection)
                assert header == 0x10 and body[:8] == b'\0\x04MQTT\x04\xc2'
                connection.sendall(b'\x20\x02\0\0')
                header, body = read_packet(connection)
                topic = b'fixture/raw/#' if mode == 'mc' else b'fixture/2/e/#'
                assert header == 0x82 and body[2:] == string(topic) + b'\0'
                handshake_ready.wait(4)
                connection.sendall(packet(0x90, body[:2] + b'\0'))
                started.wait(4)
                while True:
                    try:
                        header, body = read_packet(connection)
                    except AssertionError as error:
                        if str(error) == 'premature connection close':
                            break
                        raise
                    if header == 0xC0:
                        connection.sendall(b'\xD0\0')
                        continue
                    assert header == 0x30
                    topic_size = int.from_bytes(body[:2], 'big')
                    assert body[2:2 + topic_size].startswith(b'fixture/raw/' if mode == 'mc' else b'fixture/2/e/')
                    with lock:
                        published.append(body)
                        for target in clients:
                            try:
                                target.sendall(packet(0x30, body))
                            except OSError:
                                pass
        except Exception as error:
            errors.append(repr(error))

    def accept():
        try:
            for _ in range(2):
                connection, _ = listener.accept()
                clients.append(connection)
            workers = [threading.Thread(target=serve, args=(connection,)) for connection in clients]
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join(12)
            assert all(not worker.is_alive() for worker in workers), 'broker workers did not finish'
        except Exception as error:
            errors.append(repr(error))
        finally:
            listener.close()

    broker = threading.Thread(target=accept)
    broker.start()
    with tempfile.TemporaryDirectory(prefix='trailmate-mqtt-application-') as directory:
        processes = [subprocess.Popen([binary, str(port), mode, role, str(pathlib.Path(directory) / role)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) for role in ('A', 'B')]
        results = []
        for process in processes:
            try:
                output, _ = process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                output, _ = process.communicate()
                results.append(('timeout', output[-3000:]))
                continue
            results.append((process.returncode, output[-3000:]))
        broker.join(15)
        assert not broker.is_alive(), 'broker did not terminate'
        assert all(code == 0 for code, _ in results), (mode, results, errors)
        assert not errors, (mode, errors)
        assert len(published) >= 2, 'application did not publish both messages'


if __name__ == '__main__':
    for mode in ('mt', 'mc'):
        run(sys.argv[1], mode)
    print('MQTT application: two MT and two MC services exchanged chat; sent/inbox/provenance survived reopen; physical radio forbidden')
