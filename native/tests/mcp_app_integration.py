"""Exercise MCP against a launched CyWinTask GUI with isolated test settings."""
import json
import os
import pathlib
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

root = pathlib.Path(__file__).resolve().parents[2]
app = root / 'dist-native' / 'CyWinTask.exe'
protocol = '2026-07-28'


def available_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def rpc(url, name, arguments=None, token=None, request_id=1):
    method = 'server/discover' if name is None else 'tools/call'
    params = {} if name is None else {'name': name, 'arguments': arguments or {}}
    params['_meta'] = {
        'io.modelcontextprotocol/protocolVersion': protocol,
        'io.modelcontextprotocol/clientCapabilities': {},
    }
    request = {'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params}
    headers = {'Content-Type': 'application/json',
               'Accept': 'application/json, text/event-stream',
               'MCP-Protocol-Version': protocol,
               'Mcp-Method': method}
    if name is not None: headers['Mcp-Name'] = name
    if token: headers['Authorization'] = 'Bearer ' + token
    with urllib.request.urlopen(urllib.request.Request(
        url, json.dumps(request).encode(), headers), timeout=10) as response:
        return json.load(response)


def value(reply):
    result = reply['result']
    assert not result.get('isError', False), result
    if 'structuredContent' in result: return result['structuredContent']
    return json.loads(result['content'][0]['text'])


def launch(config_path, profile=None, token=None):
    if profile:
        config_path.write_text(json.dumps({'access_profile': profile}), encoding='utf-8')
    port = available_port()
    env = dict(os.environ, CYWINTASK_MCP_CONFIG=str(config_path))
    env.pop('CYWINTASK_MCP_TOKEN', None)
    if token: env['CYWINTASK_MCP_TOKEN'] = token
    process = subprocess.Popen([str(app), '--assistant', '--mcp-local', str(port)],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    url = f'http://127.0.0.1:{port}/mcp'
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process.poll() is not None: raise AssertionError('GUI exited before MCP started')
        try:
            rpc(url, None, token=token)
            return process, url
        except (urllib.error.URLError, TimeoutError):
            time.sleep(0.2)
    process.terminate()
    process.wait(timeout=5)
    raise AssertionError('GUI MCP server did not start')


def start_child():
    return subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(40)'],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=subprocess.CREATE_NO_WINDOW)


with tempfile.TemporaryDirectory(prefix='cywintask-mcp-test-') as directory:
    config_path = pathlib.Path(directory) / 'mcp-access.json'
    gui, url = launch(config_path)
    try:
        assert not config_path.exists(), 'Missing profile should use Free without a file'
        summary = value(rpc(url, 'system.summary'))
        assert 'process_count' in summary
        current = value(rpc(url, 'ui.state'))
        assert current['page'] == 'assistant'
        changed = value(rpc(url, 'ui.navigate', {'page': 'performance'}))
        assert changed['page'] == 'performance'
        assert value(rpc(url, 'ui.state'))['page'] == 'performance'
        rights = value(rpc(url, 'permissions.list'))
        assert all(item['allowed'] for item in rights['items'])
        invalid = rpc(url, 'ui.navigate', {'page': 'invalid'})['result']
        assert invalid.get('isError', False), 'Free bypassed argument validation'
        invalid_stop = rpc(url, 'process.terminate', {'pid': 'invalid', 'created': 1})['result']
        assert invalid_stop.get('isError', False), 'Free bypassed process argument validation'
        child = start_child()
        try:
            created = None
            for _ in range(50):
                rows = value(rpc(url, 'process.list', {'query': str(child.pid), 'limit': 100}))['items']
                created = next((row['created'] for row in rows if row['pid'] == child.pid), None)
                if created: break
                time.sleep(0.1)
            assert created, 'Owned test process was not sampled'
            stopped = value(rpc(url, 'process.terminate',
                {'pid': child.pid, 'created': created}))
            assert stopped['accepted'] and child.wait(timeout=5) is not None
        finally:
            if child.poll() is None: child.terminate()
        print('PASS real GUI Free: tokenless read, UI mutation, owned-process stop and argument validation')
    finally:
        gui.terminate()
        gui.wait(timeout=5)

    token = secrets.token_urlsafe(36)
    gui, url = launch(config_path, profile='protected', token=token)
    try:
        try:
            rpc(url, None)
            raise AssertionError('Protected GUI accepted a missing token')
        except urllib.error.HTTPError as error:
            assert error.code == 401
        assert 'process_count' in value(rpc(url, 'system.summary', token=token))
        denied = rpc(url, 'ui.navigate', {'page': 'performance'}, token=token)['result']
        assert denied.get('isError', False), 'Protected GUI accepted a forbidden mutation'
        print('PASS real GUI Protected: token required, read available, mutation controlled')
    finally:
        gui.terminate()
        gui.wait(timeout=5)
