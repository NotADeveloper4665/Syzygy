#!/usr/bin/env python3
"""Experimental SQ1: pinned QUIC, paired-certificate proof, bounded media datagrams.
Only loopback GameStream HTTPS/RTSP and three streaming UDP ports are forwarded.
"""
import argparse
import asyncio
import base64
import hashlib
import json
import os
from pathlib import Path
import secrets
import signal
import ssl
import struct
import sys
import time
from datetime import datetime, timezone
from aioquic.asyncio import connect, serve
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import HandshakeCompleted, StreamDataReceived, DatagramFrameReceived, ConnectionTerminated
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

ALPN = 'syzygy-quic-1'
DOMAIN = b'Syzygy QUIC client authentication v1\x00'
FRAGMENT = struct.Struct('!HIHH')
MAX_PACKET = 65507
MAX_BUFFER = 262144


def valid_certificate(cert):
    now = datetime.now(timezone.utc)
    return cert.not_valid_before_utc <= now <= cert.not_valid_after_utc


def fingerprint(cert):
    return cert.fingerprint(hashes.SHA256()).hex()


def paired(state_file, cert):
    try:
        nodes = json.loads(Path(state_file).read_text())['root']['named_devices']
        if not valid_certificate(cert):
            return False
        wanted = fingerprint(cert)
        return any(fingerprint(x509.load_pem_x509_certificate(n['cert'].encode())) == wanted
                   and int(n.get('perm', 0)) & ((1 << 25) | (1 << 26)) for n in nodes)
    except (OSError, ValueError, KeyError, TypeError):
        return False


class Reassembly:
    def __init__(self):
        self.pending = {}
        self.bytes = 0

    def receive(self, packet):
        now = time.monotonic()
        for key, entry in list(self.pending.items()):
            if now - entry[0] > .1:
                self.bytes -= sum(map(len, entry[2].values()))
                del self.pending[key]
        if len(packet) < FRAGMENT.size:
            return None
        port, seq, index, count = FRAGMENT.unpack_from(packet)
        chunk = packet[FRAGMENT.size:]
        if not 1 <= count <= 66 or index >= count or len(chunk) > 1000:
            return None
        key = port, seq
        if key not in self.pending:
            if len(self.pending) >= 128 or self.bytes + len(chunk) > MAX_BUFFER:
                return None
            self.pending[key] = [now, count, {}]
        entry = self.pending[key]
        if count != entry[1] or index in entry[2] or self.bytes + len(chunk) > MAX_BUFFER:
            return None
        entry[2][index] = chunk
        self.bytes += len(chunk)
        if len(entry[2]) == count:
            data = b''.join(entry[2][i] for i in range(count))
            self.bytes -= sum(map(len, entry[2].values()))
            del self.pending[key]
            return (port, data) if len(data) <= MAX_PACKET else None
        return None


class UDP(asyncio.DatagramProtocol):
    def __init__(self, owner, port, target=None):
        self.owner, self.port, self.target = owner, port, target
        self.transport = None
        self.peer = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        if not self.owner.authorized:
            return
        if self.target:
            if addr != self.target:
                return
        elif self.peer is None:
            self.peer = addr
        elif addr != self.peer:
            return
        self.owner.send_udp(self.port, data)

    def deliver(self, data):
        target = self.target or self.peer
        if target:
            self.transport.sendto(data, target)


class Tunnel(QuicConnectionProtocol):
    def __init__(self, *args, options=None, **kwargs):
        super().__init__(*args, **kwargs)
        self.options = options
        self.server = not self._quic.configuration.is_client
        self.authorized = False
        self.challenge = None
        self.cert = None
        self.buffers = {}
        self.kinds = {}
        self.writers = {}
        self.udp = {}
        self.seq = 0
        self.fragments = Reassembly()
        self.ready = asyncio.get_event_loop().create_future()
        self.tasks = set()
        self.deadline = asyncio.get_event_loop().call_later(5, self.auth_timeout)
        self.local = '127.80.%d.%d' % (secrets.randbelow(250)+1, secrets.randbelow(250)+1)
        self.base = options.base
        self.control = None
        self.next_authorization_check = 0.0

    def spawn(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.add(task)
        task.add_done_callback(self.tasks.discard)
        return task

    def auth_timeout(self):
        if not self.authorized:
            self.fail('Authentication timeout')

    def fail(self, message):
        print('QUIC protocol error: '+message, file=sys.stderr, flush=True)
        if not self.ready.done() and not self.server:
            self.ready.set_exception(RuntimeError(message))
        self.close(error_code=0x101, reason_phrase=message[:100])

    def line(self, stream, data):
        self._quic.send_stream_data(stream, json.dumps(data).encode()+b'\n')
        self.transmit()

    def quic_event_received(self, event):
        try:
            if isinstance(event, HandshakeCompleted):
                if event.alpn_protocol != ALPN:
                    raise RuntimeError('Wrong ALPN')
                if not self.server:
                    host_cert = self._quic.tls._peer_certificate
                    if host_cert is None or not valid_certificate(host_cert) or fingerprint(host_cert) != self.options.pin.lower():
                        raise RuntimeError('Host certificate mismatch')
                    self.line(0, {'hello': 1})
            elif isinstance(event, StreamDataReceived):
                if event.stream_id % 4 != 0:
                    raise RuntimeError('Unsupported stream')
                buf = self.buffers.setdefault(event.stream_id, bytearray())
                buf.extend(event.data)
                if len(buf) > MAX_BUFFER:
                    raise RuntimeError('Stream buffer limit')
                self.process(event.stream_id, event.end_stream)
            elif isinstance(event, DatagramFrameReceived):
                if self.authorized:
                    result = self.fragments.receive(event.data)
                    if result and result[0] in (self.base+9, self.base+11):
                        self.deliver_udp(*result)
            elif isinstance(event, ConnectionTerminated):
                self.authorized = False
                if not self.ready.done() and not self.server:
                    self.ready.set_exception(RuntimeError('QUIC connection terminated'))
                self.cleanup()
        except Exception as error:
            self.fail(str(error) or type(error).__name__)

    def process(self, stream, ended=False):
        buf = self.buffers[stream]
        if stream == 0:
            while b'\n' in buf:
                row, _, rest = bytes(buf).partition(b'\n')
                buf[:] = rest
                if len(row) > 32768:
                    raise RuntimeError('Oversized authentication')
                self.authentication(json.loads(row))
            if len(buf) > 32768 or ended:
                raise RuntimeError('Invalid authentication stream')
            return
        if not self.still_authorized():
            raise RuntimeError('Unauthenticated stream')
        if stream not in self.kinds:
            if b'\n' not in buf:
                return
            row, _, rest = bytes(buf).partition(b'\n')
            buf[:] = rest
            spec = json.loads(row)
            if spec == {'kind': 'control'}:
                if self.control is not None:
                    raise RuntimeError('Duplicate control stream')
                self.control = stream
                self.kinds[stream] = 'control'
            elif spec.get('kind') == 'tcp' and spec.get('port') in (self.base-5, self.base+21) and self.server:
                if len(self.writers) + sum(k == 'pending' for k in self.kinds.values()) >= 16:
                    raise RuntimeError('Too many TCP streams')
                self.kinds[stream] = 'pending'
                self.spawn(self.open_tcp(stream, spec['port']))
            else:
                raise RuntimeError('Disallowed channel')
        kind = self.kinds[stream]
        if kind == 'control':
            while len(buf) >= 4:
                length = struct.unpack_from('!I', buf)[0]
                if length > MAX_PACKET:
                    raise RuntimeError('Oversized control packet')
                if len(buf) < length+4:
                    break
                data = bytes(buf[4:length+4])
                del buf[:length+4]
                self.deliver_udp(self.base+10, data)
        elif kind == 'tcp':
            writer = self.writers.get(stream)
            if writer is None:
                buf.clear()
                return
            if buf:
                writer.write(bytes(buf))
                buf.clear()
                if writer.transport.get_write_buffer_size() > MAX_BUFFER:
                    raise RuntimeError('TCP backpressure limit')
            if ended:
                writer.close()

    def authentication(self, message):
        if self.server:
            if message == {'hello': 1} and self.challenge is None:
                self.challenge = secrets.token_bytes(32)
                self.line(0, {'challenge': base64.b64encode(self.challenge).decode()})
            elif not self.authorized and self.challenge is not None and 'certificate' in message:
                cert = x509.load_pem_x509_certificate(message['certificate'].encode())
                if not paired(self.options.state, cert):
                    raise RuntimeError('Client is not paired or has no streaming permission')
                key = cert.public_key()
                if not isinstance(key, rsa.RSAPublicKey):
                    raise RuntimeError('Unsupported client key')
                key.verify(base64.b64decode(message['signature'], validate=True), DOMAIN+self.challenge,
                           padding.PKCS1v15(), hashes.SHA256())
                self.cert = cert
                self.challenge = None
                self.spawn(self.authorize_server())
            else:
                raise RuntimeError('Invalid authentication state')
        elif 'challenge' in message and not self.authorized:
            challenge = base64.b64decode(message['challenge'], validate=True)
            if len(challenge) != 32:
                raise RuntimeError('Invalid challenge')
            key = serialization.load_pem_private_key(Path(self.options.key).read_bytes(), password=None)
            signature = key.sign(DOMAIN+challenge, padding.PKCS1v15(), hashes.SHA256())
            self.line(0, {'certificate': Path(self.options.cert).read_text(),
                          'signature': base64.b64encode(signature).decode()})
        elif message == {'authorized': True} and not self.authorized:
            self.authorized = True
            self.deadline.cancel()
            self.spawn(self.start_client())
        else:
            raise RuntimeError('Invalid authentication response')

    async def authorize_server(self):
        try:
            for port in (self.base+9, self.base+10, self.base+11):
                _, proto = await asyncio.get_event_loop().create_datagram_endpoint(
                    lambda p=port: UDP(self, p, ('127.0.0.1', p)), local_addr=(self.local, 0))
                self.udp[port] = proto
            self.authorized = True
            self.deadline.cancel()
            self.revocation_timer = asyncio.get_event_loop().call_later(1, self.review_authorization)
            self.line(0, {'authorized': True})
        except Exception as error:
            self.fail(str(error) or type(error).__name__)

    async def start_client(self):
        try:
            self.control = self._quic.get_next_available_stream_id()
            self.kinds[self.control] = 'control'
            self.line(self.control, {'kind': 'control'})
            for port in (self.base+9, self.base+10, self.base+11):
                _, proto = await asyncio.get_event_loop().create_datagram_endpoint(
                    lambda p=port: UDP(self, p), local_addr=(self.options.local, port))
                self.udp[port] = proto
            self.servers = []
            for port in (self.base-5, self.base+21):
                self.servers.append(await asyncio.start_server(
                    lambda r, w, p=port: self.spawn(self.client_tcp(r, w, p)), self.options.local, port))
            self.ready.set_result(True)
        except Exception as error:
            self.fail(str(error) or type(error).__name__)

    def review_authorization(self):
        if self.authorized:
            if not paired(self.options.state, self.cert):
                self.authorized = False
                self.fail('Client authorization revoked')
                return
            self.revocation_timer = asyncio.get_event_loop().call_later(1, self.review_authorization)

    def still_authorized(self):
        return self.authorized

    def deliver_udp(self, port, data):
        if port in self.udp and self.still_authorized():
            self.udp[port].deliver(data)

    def send_udp(self, port, data):
        if len(data) > MAX_PACKET or not self.still_authorized():
            return
        if port == self.base+10:
            if self.control is not None:
                sender = self._quic._streams[self.control].sender
                if sender._buffer_stop - sender._buffer_start > MAX_BUFFER:
                    self.fail('Control queue limit')
                    return
                self._quic.send_stream_data(self.control, struct.pack('!I', len(data))+data)
                self.transmit()
            return
        # Limit queued media. Expired packets are not retransmitted by QUIC.
        if len(self._quic._datagrams_pending) >= 256:
            return
        self.seq = (self.seq+1) & 0xffffffff
        count = max(1, (len(data)+999)//1000)
        for index in range(count):
            self._quic.send_datagram_frame(FRAGMENT.pack(port, self.seq, index, count)+data[index*1000:(index+1)*1000])
        self.transmit()

    async def open_tcp(self, stream, port):
        try:
            reader, writer = await asyncio.open_connection('127.0.0.1', port, local_addr=(self.local, 0))
            self.writers[stream] = writer
            self.kinds[stream] = 'tcp'
            self.process(stream)
            await self.pump(reader, stream)
        except Exception as error:
            self.fail(str(error) or type(error).__name__)

    async def client_tcp(self, reader, writer, port):
        stream = self._quic.get_next_available_stream_id()
        self.writers[stream] = writer
        self.kinds[stream] = 'tcp'
        self.line(stream, {'kind': 'tcp', 'port': port})
        try:
            await self.pump(reader, stream)
        except Exception as error:
            self.fail(str(error) or type(error).__name__)

    async def pump(self, reader, stream):
        try:
            while self.still_authorized():
                data = await reader.read(16384)
                if not data:
                    break
                sender = self._quic._streams[stream].sender
                if sender._buffer_stop - sender._buffer_start > MAX_BUFFER:
                    raise RuntimeError('Reliable stream queue limit')
                self._quic.send_stream_data(stream, data)
                self.transmit()
            self._quic.send_stream_data(stream, b'', end_stream=True)
            self.transmit()
        finally:
            writer = self.writers.pop(stream, None)
            if writer:
                writer.close()

    def cleanup(self):
        self.deadline.cancel()
        if hasattr(self, "revocation_timer"):
            self.revocation_timer.cancel()
        for proto in self.udp.values():
            proto.transport.close()
        for writer in self.writers.values():
            writer.close()
        for server in getattr(self, 'servers', []):
            server.close()
        for task in self.tasks:
            task.cancel()


async def main_async(options):
    configuration = QuicConfiguration(is_client=options.command == 'client', alpn_protocols=[ALPN],
                                      max_datagram_frame_size=65536, idle_timeout=20)
    configuration.load_cert_chain(options.cert, options.key)
    factory = lambda *a, **kw: Tunnel(*a, options=options, **kw)
    done = asyncio.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        asyncio.get_event_loop().add_signal_handler(sig, done.set)
    if options.command == 'server':
        server = await serve(options.host, options.port, configuration=configuration, create_protocol=factory, retry=True)
        print('SYZYGY_QUIC_LISTENING', options.port, flush=True)
        await done.wait()
        server.close()
    else:
        # Authentication is an exact saved-certificate pin, checked at the TLS
        # handshake before accepting application data. Self-signed host certs
        # do not have a public CA chain or necessarily contain a hostname SAN.
        configuration.verify_mode = ssl.CERT_NONE
        try:
            async with connect(options.host, options.port, configuration=configuration, create_protocol=factory) as tunnel:
                await asyncio.wait_for(tunnel.ready, 5)
                print('SYZYGY_QUIC_READY', flush=True)
                await done.wait()
        except (ConnectionError, asyncio.TimeoutError):
            print('QUIC unavailable', file=sys.stderr)
            return 3
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['server', 'client'])
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=48020)
    parser.add_argument('--base', type=int, default=47989)
    parser.add_argument('--cert', required=True)
    parser.add_argument('--key', required=True)
    parser.add_argument('--state')
    parser.add_argument('--pin')
    parser.add_argument('--local', default='127.90.1.1')
    args = parser.parse_args()
    if args.command == 'server' and not args.state or args.command == 'client' and not args.pin:
        parser.error('Server needs --state; client needs --pin')
    os.umask(0o077)
    try:
        sys.exit(asyncio.run(main_async(args)))
    except Exception as error:
        print('QUIC failed: '+str(error), file=sys.stderr)
        sys.exit(4)
