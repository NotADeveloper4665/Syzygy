import asyncio
import importlib.util
import json
from pathlib import Path
import ssl
import tempfile
import unittest
import sys
from types import SimpleNamespace
from datetime import datetime, timedelta, timezone
from aioquic.asyncio import serve, connect
from aioquic.quic.configuration import QuicConfiguration
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

SCRIPT = Path(__file__).resolve().parents[1] / 'transport/syzygy-quic.py'
spec = importlib.util.spec_from_file_location('sq', SCRIPT)
sq = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sq)

def credentials(root, name):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    now = datetime.now(timezone.utc)
    cert = x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=10)).sign(key, hashes.SHA256())
    certfile, keyfile = root/(name+'.crt'), root/(name+'.key')
    certfile.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    keyfile.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return cert, str(certfile), str(keyfile)

class UDPServer(asyncio.DatagramProtocol):
    def connection_made(self, transport): self.transport = transport
    def datagram_received(self, data, addr): self.transport.sendto(data, addr)

class Tests(unittest.IsolatedAsyncioTestCase):
    async def test_real_quic_authentication_media_control_and_tcp(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            host, hostcert, hostkey = credentials(root, 'host')
            client, clientcert, clientkey = credentials(root, 'client')
            state = root/'state.json'
            state.write_text(json.dumps({'root': {'named_devices': [{'cert': Path(clientcert).read_text(), 'perm': 1 << 25}]}}))
            base = 32989
            server_options = SimpleNamespace(base=base, state=str(state))
            config = QuicConfiguration(is_client=False, alpn_protocols=[sq.ALPN], max_datagram_frame_size=65536)
            config.load_cert_chain(hostcert, hostkey)
            server = await serve('127.0.0.1', 0, configuration=config, create_protocol=lambda *a, **kw: sq.Tunnel(*a, options=server_options, **kw))
            port = server._transport.get_extra_info('sockname')[1]
            transports=[]
            for p in (base+9,base+10,base+11):
                t,_=await asyncio.get_running_loop().create_datagram_endpoint(UDPServer, local_addr=('127.0.0.1', p))
                transports.append(t)
            async def echo(reader, writer):
                while data:=await reader.read(16384):
                    writer.write(data)
                    await writer.drain()
                writer.close()
            tcp = await asyncio.start_server(echo, '127.0.0.1', base+21)
            client_options=SimpleNamespace(base=base, cert=clientcert,key=clientkey,pin=sq.fingerprint(host),local='127.90.2.1')
            conf=QuicConfiguration(is_client=True, alpn_protocols=[sq.ALPN],max_datagram_frame_size=65536,verify_mode=ssl.CERT_NONE)
            try:
                async with connect('127.0.0.1', port, configuration=conf,create_protocol=lambda *a, **kw: sq.Tunnel(*a,options=client_options,**kw)) as tunnel:
                    await asyncio.wait_for(tunnel.ready, 5)
                    # Sequential HTTPS/RTSP requests must not exhaust the active-stream cap.
                    for request in range(20):
                        reader, writer=await asyncio.open_connection(client_options.local,base+21)
                        writer.write(b'RTSP test')
                        await writer.drain()
                        self.assertEqual(await asyncio.wait_for(reader.readexactly(9),3), b'RTSP test')
                        writer.close()
                        await writer.wait_closed()
                        await asyncio.sleep(.01)
                    for p in (base+9,base+10,base+11):
                        loop=asyncio.get_running_loop()
                        import socket
                        sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
                        sock.setblocking(False)
                        data=b'media'*400 if p!=base+10 else b'control'
                        await loop.sock_sendto(sock,data,(client_options.local,p))
                        received,_=await asyncio.wait_for(loop.sock_recvfrom(sock,65536),3)
                        self.assertEqual(received,data)
                        sock.close()
                for kind in ('wrong-pin', 'unpaired', 'forged-proof'):
                    bad = SimpleNamespace(**vars(client_options))
                    if kind == 'wrong-pin': bad.pin = '00'*32
                    if kind == 'unpaired':
                        _, bad.cert, bad.key = credentials(root, 'unknown')
                    if kind == 'forged-proof': bad.key = hostkey
                    async with connect('127.0.0.1', port, configuration=conf,
                            create_protocol=lambda *a, o=bad, **kw: sq.Tunnel(*a, options=o, **kw)) as rejected:
                        with self.assertRaises(RuntimeError):
                            await asyncio.wait_for(rejected.ready, 3)
                        self.assertFalse(rejected.authorized)
                async with connect('127.0.0.1', port, configuration=conf,
                        create_protocol=lambda *a, **kw: sq.Tunnel(*a, options=client_options, **kw)) as revoked:
                    await asyncio.wait_for(revoked.ready, 3)
                    state.write_text(json.dumps({'root': {'named_devices': []}}))
                    loop = asyncio.get_running_loop()
                    import socket
                    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    sock.setblocking(False)
                    await loop.sock_sendto(sock, b'revoked', (client_options.local, base+10))
                    await asyncio.wait_for(revoked.wait_closed(), 3)
                    self.assertFalse(revoked.authorized)
                    sock.close()
            finally:
                server.close()
                tcp.close()
                for t in transports:t.close()

    async def test_packaged_cli_startup_and_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            host, hostcert, hostkey = credentials(root, 'host')
            _, clientcert, clientkey = credentials(root, 'client')
            state = root/'state.json'
            state.write_text(json.dumps({'root': {'named_devices': [{'cert': Path(clientcert).read_text(), 'perm': 1 << 25}]}}))
            processes = []
            base = 33989
            async def echo(reader, writer):
                while data := await reader.read(16384):
                    writer.write(data)
                    await writer.drain()
                writer.close()
            tcp = await asyncio.start_server(echo, '127.0.0.1', base+21)
            async def start(arguments, expected):
                process = await asyncio.create_subprocess_exec(sys.executable, str(SCRIPT), *arguments,
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
                processes.append(process)
                line = await asyncio.wait_for(process.stdout.readline(), 8)
                if not line.startswith(expected):
                    self.fail('Helper startup failed: ' + (await process.stderr.read()).decode())
                return line
            try:
                import socket
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
                    reservation.bind(('127.0.0.1', 0))
                    port = reservation.getsockname()[1]
                await start(['server', '--host', '127.0.0.1', '--port', str(port), '--base', str(base),
                    '--cert', hostcert, '--key', hostkey, '--state', str(state)], b'SYZYGY_QUIC_LISTENING')
                await start(['client', '--host', '127.0.0.1', '--port', str(port), '--base', str(base),
                    '--cert', clientcert, '--key', clientkey, '--pin', sq.fingerprint(host), '--local', '127.90.3.1'], b'SYZYGY_QUIC_READY')
                reader, writer = await asyncio.open_connection('127.90.3.1', base+21)
                writer.write(b'CLI RTSP')
                await writer.drain()
                self.assertEqual(await asyncio.wait_for(reader.readexactly(8), 3), b'CLI RTSP')
                writer.close()
                await writer.wait_closed()
            finally:
                for process in reversed(processes):
                    if process.returncode is None:
                        process.terminate()
                        await asyncio.wait_for(process.wait(), 5)
                tcp.close()
                await tcp.wait_closed()

    def test_reassembly_limits_reordering(self):
        r=sq.Reassembly()
        self.assertIsNone(r.receive(sq.FRAGMENT.pack(9,1,1,2)+b'b'))
        self.assertEqual(r.receive(sq.FRAGMENT.pack(9,1,0,2)+b'a'),(9,b'ab'))
        self.assertIsNone(r.receive(sq.FRAGMENT.pack(9,2,0,1000)+b'bad'))
        for i in range(1000):r.receive(sq.FRAGMENT.pack(9,i,0,2)+b'x'*1000)
        self.assertLessEqual(len(r.pending),128)
        self.assertLessEqual(r.bytes,sq.MAX_BUFFER)

if __name__=='__main__':unittest.main()
