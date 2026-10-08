#!/usr/bin/env python3
"""Exercise the installed host's PIN-free enrollment and certificate pinning."""
import hashlib
import hmac
import json
import os
from pathlib import Path
import ssl
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
    key = key_result.stdout.strip()
    assert len(key) == 48
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
        with urlopen(url + urlencode(dict(uniqueid=uid, **values)), context=context, timeout=4) as reply:
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
        process = subprocess.Popen(['/usr/bin/syzygy', 'capture=x11', 'encoder=software', 'port=49199'],
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
                assert int(info.findtext('Permission')) == ((31 << 8) | (31 << 16) | (7 << 24)), 'Passkey did not grant full permissions'

            # Retry after a lost response: same cert must remain a single paired device.
            uid, data, message, values = challenge(0)
            assert request(uid, values).attrib['status_code'] == '200'
            state = json.loads((root / 'sunshine/sunshine_state.json').read_text())
            assert len(state['root']['named_devices']) == 2, 'Retry created duplicate devices'
        finally:
            stop(process)
        process = start(log)
        try:
            assert request('eclipse-passkey-test-1', dict(phrase='pairchallenge'), context).findtext('paired') == '1', 'Pairing did not survive restart'
        finally:
            stop(process)
        log.seek(0)
        assert key not in log.read(), 'Passkey leaked into unattended startup logs'
    print('PASS: wrong passkeys rejected, one-use challenges, two clients, host confirmation, TLS client auth, retry, persistence, and log redaction')
