#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real C HTTP/Outbox, synthetic SAV authority, response faults and process restart."""
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import threading
import time


def run(binary, fault):
    requests = []
    commits = {}
    revision = 7
    saved = None
    lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_PUT(self):
            nonlocal revision, saved
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            key = body['request_id']
            identity = (body['expected_revision'], body['save_data'])
            with lock:
                requests.append((key, identity))
                first = len(requests) == 1
                if first and fault == 'http-503':
                    self.send_response(503)
                    self.end_headers()
                    self.wfile.write(b'{"message":"temporary rejection"}')
                    return
                if key in commits:
                    assert commits[key][0] == identity
                    result = commits[key][1]
                else:
                    assert body['expected_revision'] == revision
                    revision += 1
                    saved = base64.b64decode(body['save_data'])
                    result = revision
                    commits[key] = identity, result
            response = json.dumps({'revision': result}).encode()
            if first and fault == 'lost-200':
                self.send_response(200)
                self.send_header('Content-Length', str(len(response)))
                self.end_headers()
                self.close_connection = True
                return
            if first and fault == 'truncated-200':
                self.send_response(200)
                self.send_header('Content-Length', str(len(response)))
                self.end_headers()
                self.wfile.write(response[:-1])
                return
            if first and fault == 'delay-35s':
                time.sleep(35)
            self.send_response(200)
            self.send_header('Content-Length', str(len(response)))
            self.end_headers()
            try:
                self.wfile.write(response)
            except (BrokenPipeError, ConnectionResetError):
                pass

    with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server, tempfile.TemporaryDirectory() as cwd:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        url = f'http://127.0.0.1:{server.server_port}'
        try:
            subprocess.run([binary, url, 'start'], cwd=cwd, check=True, timeout=45)
            subprocess.run([binary, url, 'resume'], cwd=cwd, check=True, timeout=45)
            assert len(requests) == 3 and requests[0] == requests[1]
            assert requests[2][1][0] == 8 and revision == 9
            assert saved == bytes([5, 6, 7, 8]) and len(commits) == 2
            print(f'{fault}: same request replay, latest SAV, process restart, revisions 7->8->9 PASS', flush=True)
        finally:
            server.shutdown()
            worker.join()


if __name__ == '__main__':
    binary = str(Path(sys.argv[1]).resolve())
    for fault in ('lost-200', 'delay-35s', 'truncated-200', 'http-503'):
        run(binary, fault)
