"""MT/MC/VMP topic separation against an independent loopback wire peer."""
import socket
import struct
import subprocess
import sys
import threading
from linux_mqtt_client_test import packet, read_packet, string


def varint(value):
    result = b''
    while value > 127:
        result += bytes([(value & 127) | 128])
        value >>= 7
    return result + bytes([value])


def envelope(sender, destination, identifier, payload):
    data = b'\x08\x01\x12' + varint(len(payload)) + payload
    mesh = b'\x0D' + struct.pack('<I', sender) + b'\x15' + struct.pack('<I', destination) + b'\x22' + varint(len(data)) + data + b'\x35' + struct.pack('<I', identifier)
    return b'\x0A' + varint(len(mesh)) + mesh + b'\x12' + string(b'primary')[1:] + b'\x1A' + string(('!%08x' % sender).encode())[1:]


def run(binary, mode, ipv6=False):
    listener = socket.socket(socket.AF_INET6 if ipv6 else socket.AF_INET, socket.SOCK_STREAM)
    try:
        listener.bind(('::1' if ipv6 else '127.0.0.1', 0))
    except OSError:
        listener.close()
        if ipv6:
            return False
        raise
    port = listener.getsockname()[1]
    listener.listen(1)
    listener.settimeout(4)
    errors = []

    def serve():
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(3)
                assert read_packet(connection) == (0x10, b'\0\x04MQTT\x04\xc2\0\x01' + string(b'tm-fixture') + string(b'u') + string(b'p'))
                connection.sendall(b'\x20\x02\0\0')
                header, body = read_packet(connection)
                subscription = b'fixture/raw/#' if mode == 'mc' else b'fixture/2/e/#'
                assert header == 0x82 and body[2:] == string(subscription) + b'\0'
                connection.sendall(packet(0x90, body[:2] + b'\0'))
                if mode == 'cancel':
                    assert connection.recv(1) == b'', 'cancelled publish escaped queue'
                    return
                if mode == 'mc':
                    raw = b'\x59\x40\xaa\xbb\xcc'
                    connection.sendall(packet(0x31, string(b'fixture/raw/peer') + raw) +
                                       packet(0x30, string(b'fixture/raw/tm-fixture') + raw) +
                                       packet(0x30, string(b'fixture/raw/peer') + raw))
                    assert read_packet(connection) == (0x30, string(b'fixture/raw/tm-fixture') + raw)
                else:
                    mt = envelope(2, 1, 0x01020304, b'x' * 233)
                    connection.sendall(packet(0x30, string(b'fixture/2/e/primary/!00000002') + mt) +
                                       packet(0x30, string(b'fixture/2/e/vmp') + b'vmp-carrier'))
                    assert read_packet(connection) == (0x30, string(b'fixture/2/e/primary/!00000001') + envelope(1, 2, 7, b'out'))
                assert connection.recv(1) == b''
        except Exception as error:
            errors.append(repr(error))
        finally:
            listener.close()

    worker = threading.Thread(target=serve)
    worker.start()
    result = subprocess.run([binary, '::1' if ipv6 else '127.0.0.1', str(port), mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=6)
    worker.join(5)
    assert not worker.is_alive(), 'broker fixture did not finish'
    assert result.returncode == 0, (mode, result.stdout[-2500:])
    assert not errors, (mode, errors)
    return True


if __name__ == '__main__':
    for mode in ('mt', 'mc', 'cancel'):
        run(sys.argv[1], mode)
    ipv6 = run(sys.argv[1], 'mt', ipv6=True)
    print('MQTT bridge: MT, MC, cancellation and receive-cohort checks passed; IPv6=' + str(ipv6))
