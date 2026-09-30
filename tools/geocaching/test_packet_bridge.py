import asyncio
import unittest

from websockets.asyncio.client import connect
from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed, InvalidStatus

from packet_bridge import HDLCReader, PacketBridge, hdlc_frame


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_raw_packets_round_trip_and_origin_policy(self):
        async def echo(reader, writer):
            try:
                while data := await reader.read(4096):
                    writer.write(data)
                    await writer.drain()
            finally:
                writer.close()
                await writer.wait_closed()

        tcp = await asyncio.start_server(echo, '127.0.0.1', 0)
        async with tcp:
            bridge = PacketBridge('127.0.0.1', tcp.sockets[0].getsockname()[1])
            async with serve(bridge.connection, '127.0.0.1', 0, origins=['http://localhost:8080'],
                             max_size=500, compression=None) as websocket:
                url = f'ws://127.0.0.1:{websocket.sockets[0].getsockname()[1]}'
                async with connect(url, origin='http://localhost:8080') as client:
                    packet = bytes(range(256)) + bytes(range(244))
                    await client.send(packet)
                    self.assertEqual(await asyncio.wait_for(client.recv(), 3), packet)
                    await client.send('{"bbox":[0,0,1,1]}')
                    with self.assertRaises(ConnectionClosed):
                        await client.recv()
                with self.assertRaises(InvalidStatus):
                    async with connect(url, origin='https://unrelated.example'):
                        self.fail('Foreign origin accepted')

    def test_hdlc_split_frames_and_bounded_oversize(self):
        packet = bytes(range(100)) + b'\x7e\x7d'
        decoder, output = HDLCReader(), []
        for byte in hdlc_frame(packet) + hdlc_frame(packet):
            output.extend(decoder.feed(bytes([byte])))
        self.assertEqual(output, [packet, packet])
        with self.assertRaises(ValueError):
            list(HDLCReader().feed(b'\x7e' + bytes(501)))


if __name__ == '__main__':
    unittest.main()
