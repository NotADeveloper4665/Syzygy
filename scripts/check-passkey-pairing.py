#!/usr/bin/env python3
"""Exercise the installed host's PIN-free enrollment and certificate pinning."""
import hashlib
import hmac
import json
import os
from pathlib import Path
import ssl
import socket
import struct
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from urllib.parse import urlencode
from urllib.request import urlopen


def field(value):
    return struct.pack('!I', len(value)) + value


def proof(key, message):
    return hmac.new(key.encode(), message, hashlib.sha256).hexdigest()


with tempfile.TemporaryDirectory(prefix='syzygy-pair-check-') as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_CONFIG_HOME=directory)
    for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
        env.pop(name, None)
    key_result = subprocess.run(['/usr/bin/syzygy', '-psk'], env=env,
                                capture_output=True, text=True, check=True)
    phrase = key_result.stdout.strip()
    key = phrase
    assert len(key.split()) == 6
    key = hashlib.scrypt(key.encode(), salt=b'Syzygy pairing phrase v2', n=32768, r=8, p=1, maxmem=64*1024*1024, dklen=24).hex()
    clients = []
    for index in range(2):
        private, cert = root / f'client{index}.key', root / f'client{index}.crt'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(private), '-out', str(cert), '-days', '1',
                        '-subj', f'/CN=Eclipse-test-{index}'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        clients.append((private, cert))

    def request(uid, values, context=None, command='pair'):
        port = 49194 if context else 49199
        url = f'{"https" if context else "http"}://127.0.0.1:{port}/{command}?'
        with urlopen(url + urlencode(dict(uniqueid=uid, **values)), context=context, timeout=30 if command == 'launch' else 4) as reply:
            return ET.fromstring(reply.read())

    def challenge(index):
        private, cert = clients[index]
        uid = f'eclipse-passkey-test-{index}'
        data = request(uid, dict(syzygyphase='challenge', devicename=f'Eclipse {index}',
                                 clientcert=cert.read_bytes().hex()))
        assert data.attrib['status_code'] == '200', data.attrib
        nonce = bytes.fromhex(data.findtext('challenge'))
        message = b'Syzygy pairing v1' + field(nonce) + field(uid.encode()) + field(hashlib.sha256(cert.read_bytes()).digest())
        assert bytes.fromhex(data.findtext('authmessage')) == message
        signature = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(private)],
                                   input=message, capture_output=True, check=True).stdout.hex()
        values = dict(syzygyphase='response', clientcert=cert.read_bytes().hex(),
                      syzygyproof=proof(key, message), clientsignature=signature)
        return uid, data, message, values

    def start(log):
        # Deliberately omit -s: ordinary startup must enable passkey enrollment.
        arguments = ['/usr/bin/syzygy', 'capture=x11', 'encoder=software', 'tails_pairing=enabled', 'port=49199']
        if env.get('SYZYGY_WAN_INTEGRATION_ENABLED') == '1':
            arguments = ['/usr/bin/syzygy', '-s', '-foreground', '-auto', '-headless', '-software', '-h264', 'tails_pairing=enabled', 'port=49199']
        process = subprocess.Popen(arguments,
                                   cwd=directory, env=env, stdout=log, stderr=subprocess.STDOUT)
        for attempt in range(100):
            try:
                with urlopen('http://127.0.0.1:49199/serverinfo', timeout=1):
                    return process
            except Exception:
                if process.poll() is not None:
                    log.seek(0)
                    raise AssertionError('Host exited:\n' + log.read())
                time.sleep(0.2)
        process.terminate()
        process.wait(timeout=5)
        raise AssertionError('Host did not start')

    def stop(process):
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

    with open(root / 'host.log', 'w+') as log:
        process = start(log)
        try:
            denied = request('tails-denied', dict(syzygyphase='challenge', syzygytails='1', clientcert=clients[0][1].read_bytes().hex()))
            assert denied.attrib['status_code'] == '403', 'Non-Tailscale socket was automatically authorized'
            uid, data, message, values = challenge(0)
            wrong = dict(values, syzygyproof=proof('b' * 48 if key != 'b' * 48 else 'a' * 48, message))
            assert request(uid, wrong).attrib['status_code'] == '401', 'Wrong key was accepted'
            assert request(uid, values).attrib['status_code'] == '400', 'Failed challenge was reusable'

            uid, data, message, values = challenge(0)
            substitute = dict(values, clientcert=clients[1][1].read_bytes().hex())
            assert request(uid, substitute).attrib['status_code'] == '401', 'Substituted certificate was accepted'
            uid, data, message, values = challenge(0)
            broken_signature = dict(values, clientsignature='00' * 256)
            assert request(uid, broken_signature).attrib['status_code'] == '401', 'Unsigned enrollment was accepted'

            # Keep both clients pending to check that their identities do not collide.
            pending = [challenge(index) for index in range(2)]
            for index, (uid, data, message, values) in enumerate(pending):
                response = request(uid, values)
                assert response.attrib['status_code'] == '200', response.attrib
                assert response.findtext('paired') == '1'
                server_cert = bytes.fromhex(data.findtext('plaincert'))
                expected = proof(key, b'Syzygy server confirmation v1' + message + hashlib.sha256(server_cert).digest())
                assert hmac.compare_digest(response.findtext('serverproof'), expected)
                assert request(uid, values).attrib['status_code'] == '400', 'Successful challenge was replayable'
                context = ssl.create_default_context(cadata=server_cert.decode())
                context.check_hostname = False  # Self-signed host cert has no hostname SAN.
                context.load_cert_chain(str(clients[index][1]), str(clients[index][0]))
                assert request(uid, dict(phrase='pairchallenge'), context).findtext('paired') == '1'
                info = request(uid, {}, context, 'serverinfo')
                assert info.findtext('SyzygyWanVersion') == '1', 'Missing WAN negotiation capability'
                assert request(uid, {}, context, 'wan').attrib['status_code'] == '404', 'WAN endpoint accepted an idle session'
                assert int(info.findtext('Permission')) == ((31 << 8) | (31 << 16) | (7 << 24)), 'Passkey did not grant full permissions'

            # Retry after a lost response: same cert must remain a single paired device.
            uid, data, message, values = challenge(0)
            assert request(uid, values).attrib['status_code'] == '200'
            state = json.loads((root / 'sunshine/sunshine_state.json').read_text())
            assert len(state['root']['named_devices']) == 2, 'Retry created duplicate devices'

            if env.get('SYZYGY_WAN_INTEGRATION_ENABLED') == '1':
                # Real native session: setup, software encoder, bounded UDP and
                # authenticated telemetry. A second paired certificate cannot tune it.
                context0 = ssl.create_default_context(cadata=server_cert.decode())
                context0.check_hostname = False
                context0.load_cert_chain(str(clients[0][1]), str(clients[0][0]))
                apps = request('eclipse-passkey-test-1', {}, context, 'applist')
                desktop = next(app for app in apps.findall('App') if app.findtext('AppTitle') == 'Desktop')
                launch = request('eclipse-passkey-test-1', dict(appid=desktop.findtext('ID'),
                    mode='640x480x30', rikey='00'*16, rikeyid='1', corever='0', wan='1',
                    localAudioPlayMode='0', surroundAudioInfo='196610'), context, 'launch')
                assert launch.attrib['status_code'] == '200', launch.attrib
                def rtsp(method, target='streamid=video', body=''):
                    # CSeq is informational for this independent request connection.
                    wire = (f'{method} {target} RTSP/1.0\r\nCSeq: 1\r\nSession: DEADBEEFCAFE\r\n'
                            f'Content-Length: {len(body.encode())}\r\n\r\n' + body).encode()
                    with socket.create_connection(('127.0.0.1', 49220), timeout=5) as connection:
                        connection.sendall(wire)
                        response = b''
                        while b'\r\n\r\n' not in response:
                            chunk = connection.recv(65536)
                            if not chunk: break
                            response += chunk
                        assert response.startswith(b'RTSP/1.0 200'), response
                    return response
                for channel in ('audio', 'video', 'control'): rtsp('SETUP', 'streamid='+channel)
                attributes = {
                    'x-nv-audio.surround.numChannels': 2, 'x-nv-audio.surround.channelMask': 3,
                    'x-nv-audio.surround.AudioQuality': 0, 'x-nv-video[0].packetSize': 1392,
                    'x-nv-video[0].clientViewportHt': 480, 'x-nv-video[0].clientViewportWd': 640,
                    'x-nv-video[0].maxFPS': 30, 'x-nv-vqos[0].bw.maximumBitrateKbps': 4000,
                    'x-nv-video[0].videoEncoderSlicesPerFrame': 1, 'x-nv-video[0].maxNumReferenceFrames': 1,
                    'x-nv-general.featureFlags': 0, 'x-ss-video[0].intraRefresh': 1,
                }
                sdp = 'v=0\r\ns=WAN integration\r\n' + ''.join(f'a={key}:{value}\r\n' for key,value in attributes.items())
                rtsp('ANNOUNCE', body=sdp)
                rtsp('PLAY')
                video = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                audio = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                video.settimeout(.5)
                video.sendto(b'PING', ('127.0.0.1', 49208))
                audio.sendto(b'PING', ('127.0.0.1', 49210))
                try:
                    for attempt in range(40):
                        video.sendto(b'PING', ('127.0.0.1', 49208))
                        audio.sendto(b'PING', ('127.0.0.1', 49210))
                        feedback = request('eclipse-passkey-test-1', {}, context, 'wan')
                        if feedback.findtext('runtimeBitrate') == '1': break
                        time.sleep(.1)
                    assert feedback.findtext('encoderOverrides') == '1', 'WAN software encoder overrides not active'
                    assert feedback.findtext('runtimeBitrate') == '1', 'Software runtime bitrate not active'
                    packet, _ = video.recvfrom(65536)
                    assert 0 < len(packet) <= 1120, 'WAN UDP packet exceeded conservative payload budget'
                    denied = request('eclipse-passkey-test-0', dict(rtt='30',jitter='0',loss='0'), context0, 'wan')
                    assert denied.attrib['status_code'] == '404', 'A second certificate changed this stream'
                    healthy = request('eclipse-passkey-test-1', dict(rtt='30',jitter='1',loss='0'), context, 'wan')
                    assert healthy.findtext('fec') == '5'
                    time.sleep(.25)
                    congested = request('eclipse-passkey-test-1', dict(rtt='150',jitter='30',loss='60'), context, 'wan')
                    assert congested.findtext('fec') == '5', 'Congestion inflated FEC'
                    assert int(congested.findtext('bitrate')) < int(healthy.findtext('bitrate'))
                    time.sleep(.25)
                    loss = request('eclipse-passkey-test-1', dict(rtt='30',jitter='1',loss='60'), context, 'wan')
                    assert loss.findtext('fec') == '35'
                    bad = request('eclipse-passkey-test-1', dict(rtt='-1',jitter='0',loss='0'), context, 'wan')
                    assert bad.attrib['status_code'] == '400'
                    time.sleep(3.2)
                    conservative = request('eclipse-passkey-test-1', {}, context, 'wan')
                    assert conservative.findtext('fec') == '10', 'Lost telemetry did not restore conservative FEC'
                    print('PASS: real WAN session, software encoder, small UDP, isolated telemetry, congestion/loss adaptation and stale fallback')
                finally:
                    video.close(); audio.close()
        except Exception:
            print('Host launcher exit:', process.poll())
            print((root / 'host.log').read_text(errors='replace')[-16000:])
            raise
        finally:
            stop(process)
        process = start(log)
        try:
            assert request('eclipse-passkey-test-1', dict(phrase='pairchallenge'), context).findtext('paired') == '1', 'Pairing did not survive restart'
        finally:
            stop(process)
        log.seek(0)
        output = log.read()
        assert phrase not in output and key not in output, 'Passkey leaked into unattended startup logs'
    print('PASS: wrong passkeys rejected, one-use challenges, two clients, host confirmation, TLS client auth, retry, persistence, and log redaction')
