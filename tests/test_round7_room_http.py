# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real C ROOM leave/terminal code against a disposable HTTP peer."""
import json
import os
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import subprocess
import threading
import unittest


class Round7RoomHTTPTests(unittest.TestCase):
    def test_preflight_room_keyboard_and_leave_without_reauthentication(self):
        requests = []
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args): pass
            def do_POST(self):
                self.rfile.read(int(self.headers['Content-Length']))
                requests.append(self.path)
                data=b'{"left":true}'
                self.send_response(200)
                self.send_header('Content-Length',str(len(data)))
                self.end_headers();self.wfile.write(data)
        with HTTPServer(('127.0.0.1',0),Handler) as server:
            thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            try:
                binary = os.environ.get('INTEGRAL_ROOM_TEST_BINARY', str(Path(__file__).resolve().parents[1] / 'c_client/build/client_room_test'))
                result=subprocess.run([binary,'--preflight-http',f'http://127.0.0.1:{server.server_port}'],capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('Preflight terminal stops retries',result.stdout)
            finally:
                server.shutdown();thread.join()
        self.assertEqual(requests,['/room-matching/leave'])

    def test_stop_leave_and_fenced_terminal_ack(self):
        requests = []

        class Handler(BaseHTTPRequestHandler):
            current_reads = 0
            def log_message(self, *_args):
                pass

            def do_GET(self):
                Handler.current_reads += 1
                failed = Handler.current_reads % 2 == 1
                data = b'{"error":"offline"}' if failed else b'{"room":null}'
                self.send_response(503 if failed else 200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                requests.append((self.path, body))
                fail = self.path.endswith('/terminate') and sum(p.endswith('/terminate') for p, _ in requests) == 1
                data = b'{"error":"unavailable"}' if fail else b'{"left":true,"media_session":{"status":"EXPIRED"}}'
                if self.path == '/room-matching/leave' and sum(p == self.path for p, _ in requests) == 1:
                    data = b'{"left":false}'
                self.send_response(503 if fail else 200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        with HTTPServer(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                binary = os.environ.get('INTEGRAL_ROOM_TEST_BINARY', str(Path(__file__).resolve().parents[1] / 'c_client/build/client_room_test'))
                result = subprocess.run([str(binary), '--round7-http', f'http://127.0.0.1:{server.server_port}'],
                                        capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            finally:
                server.shutdown()
                thread.join()
        self.assertEqual([p for p, _ in requests], ['/game/stop', '/game/stop', '/room-matching/leave', '/room-matching/leave',
            '/n64-runtime-media-sessions/media-test/terminate', '/n64-runtime-media-sessions/media-test/terminate'])
        self.assertEqual(requests[5][1], {'room_code': '12345'})
        self.assertEqual(requests[5], requests[4])


if __name__ == '__main__':
    unittest.main()
