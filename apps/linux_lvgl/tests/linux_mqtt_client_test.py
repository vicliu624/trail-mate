"""Independent MQTT wire peer; loopback only, no external broker or radio."""
import socket
import struct
import subprocess
import sys
import threading
import time


def exact(connection, count):
    result = b''
    while len(result) < count:
        part = connection.recv(count - len(result))
        assert part, 'premature connection close'
        result += part
    return result


def read_packet(connection):
    header = exact(connection, 1)[0]
    remaining, multiplier = 0, 1
    for _ in range(4):
        digit = exact(connection, 1)[0]
        remaining += (digit & 127) * multiplier
        assert remaining <= 1024
        if not digit & 128:
            return header, exact(connection, remaining)
        multiplier *= 128
    raise AssertionError('bad client remaining length')


def string(value):
    return struct.pack('!H', len(value)) + value


def packet(header, body):
    size = len(body)
    encoded = b''
    while True:
        digit, size = size % 128, size // 128
        encoded += bytes([digit | (128 if size else 0)])
        if not size:
            return bytes([header]) + encoded + body


def handshake(connection, scenario):
    header, body = read_packet(connection)
    expected = b'\0\x04MQTT\x04\xc2\0\x01' + string(b'tm-fixture') + string(b'u') + string(b'p')
    assert header == 0x10 and body == expected, ('bad CONNECT', header, len(body))
    if scenario == 'no_connack':
        assert connection.recv(1) == b''
        return False
    if scenario == 'refused':
        connection.sendall(b'\x20\x02\x00\x05')
        assert connection.recv(1) == b''
        return False
    # Split both the fixed header and body across distinct writes.
    for byte in b'\x20\x02\x00\x00':
        connection.sendall(bytes([byte]))
        time.sleep(.002)
    header, body = read_packet(connection)
    assert header == 0x82 and body[2:] == string(b'fixture/#') + b'\0'
    identifier = struct.unpack('!H', body[:2])[0]
    assert identifier
    if scenario == 'bad_suback':
        connection.sendall(packet(0x90, struct.pack('!H', identifier + 1) + b'\0'))
        assert connection.recv(1) == b''
        return False
    connection.sendall(packet(0x90, body[:2] + b'\0'))
    return True


def run(binary, scenario):
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(2)
    listener.settimeout(4)
    errors = []

    def serve():
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(3)
                if not handshake(connection, scenario):
                    return
                if scenario == 'baseline':
                    incoming = packet(0x3B, string(b'fixture/in') + b'\x00\x11\x09\x00\x08')
                    stale = packet(0x30, string(b'fixture/old') + b'\x06')
                    unrelated = packet(0x30, string(b'outside/in') + b'\x07')
                    connection.sendall(incoming + stale + unrelated)
                    published, acknowledged = False, False
                    while not (published and acknowledged):
                        header, body = read_packet(connection)
                        if header == 0x30:
                            assert body == string(b'fixture/out') + b'\x01\x00\x02'
                            published = True
                        elif header == 0x40:
                            assert body == b'\x00\x11'
                            acknowledged = True
                        else:
                            raise AssertionError(('unexpected client packet', header))
                    assert read_packet(connection) == (0xC0, b'')
                    connection.sendall(b'\xD0\x00')
                    time.sleep(.02)
                elif scenario == 'budget':
                    assert connection.recv(1) == b'', 'disabled queue leaked a PUBLISH'
                    return
                else:
                    invalid = {'oversize': b'\x30\x81\x08', 'partial': b'\x30\x06\x00\x01a',
                               'bad_utf8': b'\x30\x03\x00\x01\x00', 'qos2': b'\x34'}[scenario]
                    connection.sendall(invalid)
                    assert connection.recv(1) == b''
                    return
            if scenario == 'baseline':
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(3)
                    assert handshake(connection, scenario)
                    connection.sendall(packet(0x30, string(b'fixture/in') + b'\x04\x00\x05'))
                    assert connection.recv(1) == b''
        except Exception as error:
            errors.append(repr(error))
        finally:
            listener.close()

    worker = threading.Thread(target=serve)
    worker.start()
    mode = scenario if scenario in ('baseline', 'budget') else 'failure'
    result = subprocess.run([binary, '127.0.0.1', str(listener.getsockname()[1]), mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=7)
    worker.join(5)
    assert not worker.is_alive(), 'broker fixture did not terminate'
    assert result.returncode == 0, (scenario, result.stdout[-2500:])
    assert not errors, (scenario, errors)


if __name__ == '__main__':
    for scenario in ('baseline', 'budget', 'no_connack', 'refused', 'bad_suback', 'oversize', 'partial', 'bad_utf8', 'qos2'):
        run(sys.argv[1], scenario)
    print('MQTT client: nine independent loopback scenarios passed')
