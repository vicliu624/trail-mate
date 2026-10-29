"""Isolated Wayland wire fixture; never connects to the desktop compositor."""
import array
import os
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading


def packed_string(value):
    raw = value.encode() + b'\0'
    return struct.pack('=I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


def scenario(binary, count=1, selection='', reject=False, reject_apply=False, worker=False, topology=''):
    with tempfile.TemporaryDirectory(prefix='trailmate-gamma-') as directory:
        path = str(pathlib.Path(directory) / 'wayland-test')
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(path)
        server.listen(1)
        server.settimeout(5)
        evidence = {'claims': [], 'ramps': [], 'closed': False, 'errors': []}

        def serve():
            connection = None
            received_fds = []
            try:
                connection, _ = server.accept()
                connection.settimeout(5)
                data = b''
                bindings = {}
                gamma = None

                def event(obj, opcode, payload=b''):
                    connection.sendall(struct.pack('=II', obj, ((8 + len(payload)) << 16) | opcode) + payload)

                while True:
                    incoming, ancillary, flags, _ = connection.recvmsg(4096, socket.CMSG_SPACE(16))
                    if not incoming:
                        evidence['closed'] = True
                        break
                    assert not flags & socket.MSG_CTRUNC
                    for level, kind, content in ancillary:
                        assert (level, kind) == (socket.SOL_SOCKET, socket.SCM_RIGHTS)
                        fds = array.array('i')
                        fds.frombytes(content[:len(content) - len(content) % fds.itemsize])
                        received_fds.extend(fds)
                    data += incoming
                    while len(data) >= 8:
                        obj, header = struct.unpack_from('=II', data)
                        length, opcode = header >> 16, header & 65535
                        if len(data) < length:
                            break
                        payload, data = data[8:length], data[length:]
                        if obj == 1 and opcode == 1:
                            registry, = struct.unpack('=I', payload)
                            event(registry, 0, struct.pack('=I', 100) + packed_string('zwlr_gamma_control_manager_v1') + struct.pack('=I', 1))
                            for index in range(count):
                                event(registry, 0, struct.pack('=I', 200 + index) + packed_string('wl_output') + struct.pack('=I', 4))
                        elif obj == 1 and opcode == 0:
                            callback, = struct.unpack('=I', payload)
                            event(callback, 0, struct.pack('=I', 0))
                            event(1, 1, struct.pack('=I', callback))
                        elif obj == 2:
                            global_id, string_size = struct.unpack_from('=II', payload)
                            interface = payload[8:8 + string_size - 1].decode()
                            version, new_id = struct.unpack_from('=II', payload, 8 + (string_size + 3) // 4 * 4)
                            bindings[new_id] = interface
                            if interface == 'wl_output':
                                assert version == 4
                                event(new_id, 4, packed_string('DSI-test-' + str(global_id - 200)))
                                event(new_id, 2)
                        elif bindings.get(obj) == 'zwlr_gamma_control_manager_v1':
                            assert opcode == 0
                            gamma, output = struct.unpack('=II', payload)
                            evidence['claims'].append(output)
                            event(gamma, 1 if reject else 0, b'' if reject else struct.pack('=I', 257))
                        elif obj == gamma:
                            assert opcode == 0 and not payload
                            assert received_fds
                            fd = received_fds.pop(0)
                            try:
                                ramp = array.array('H')
                                ramp.frombytes(os.pread(fd, 65536, 0))
                            finally:
                                os.close(fd)
                            evidence['ramps'].append(ramp)
                            if reject_apply:
                                event(gamma, 1)
                            if topology == 'remove':
                                event(registry, 1, struct.pack('=I', 200))
                            if topology == 'add':
                                event(registry, 0, struct.pack('=I', 299) + packed_string('wl_output') + struct.pack('=I', 4))
                        else:
                            raise AssertionError(('unexpected request', obj, opcode))
            except (ConnectionResetError, BrokenPipeError):
                # A client can close with unread delete_id events after its
                # final sync. AF_UNIX may report reset instead of an empty read.
                evidence['closed'] = True
            except Exception as error:
                evidence['errors'].append(repr(error))
            finally:
                for fd in received_fds:
                    os.close(fd)
                if connection:
                    connection.close()

        thread = threading.Thread(target=serve)
        thread.start()
        command = [binary, path, selection, '217'] + (['worker'] if worker else [])
        result = subprocess.run(command, capture_output=True, text=True, timeout=8)
        thread.join(6)
        server.close()
        assert not thread.is_alive(), 'fixture did not terminate'
        assert result.returncode == 0, result.stderr
        assert not evidence['errors'], evidence['errors']
        assert evidence['closed'], 'client retained gamma ownership'
        if count > 1 and not selection or selection == 'missing':
            assert 'status=2' in result.stdout, result.stdout
            assert not evidence['claims'] and not evidence['ramps']
        elif reject:
            assert 'status=1' in result.stdout, result.stdout
            assert not evidence['ramps']
        else:
            assert 'status=0' in result.stdout, result.stdout
            assert ('applied=0' if reject_apply or topology else 'applied=1') in result.stdout, result.stdout
            if not reject_apply and not topology:
                assert 'healthy=1' in result.stdout, result.stdout
            assert len(evidence['claims']) == len(evidence['ramps']) == 1
            ramp = evidence['ramps'][0]
            assert len(ramp) == 257 * 3
            expected = [round(i * 65535 * 217 / (256 * 255)) for i in range(257)]
            for channel in range(3):
                actual = list(ramp[channel * 257:(channel + 1) * 257])
                assert all(abs(a - b) <= 1 for a, b in zip(actual, expected))
                assert actual[0] == 0 and actual[-1] == 65535 * 217 // 255
                assert actual == sorted(actual)


if __name__ == '__main__':
    binary = sys.argv[1]
    scenario(binary)
    scenario(binary, count=2)
    scenario(binary, count=2, selection='DSI-test-1')
    scenario(binary, selection='missing')
    scenario(binary, reject=True)
    scenario(binary, reject_apply=True)
    scenario(binary, worker=True)
    scenario(binary, count=2, selection='DSI-test-1', worker=True)
    scenario(binary, topology='remove')
    scenario(binary, topology='add')
    print('gamma control: ten isolated compositor/worker scenarios passed')
