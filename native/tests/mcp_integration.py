import json
import os
import pathlib
import queue
import secrets
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

root = pathlib.Path(__file__).resolve().parents[2]
cli = root / 'dist-native' / 'CyWinTaskCli.exe'

help_result = subprocess.run([str(cli), '--help'], text=True, capture_output=True, timeout=8)
assert help_result.returncode == 0 and 'invoke NAME' in help_result.stdout
listed = subprocess.run([str(cli), 'list'], text=True, capture_output=True, timeout=8)
assert listed.returncode == 0 and 'system.summary' in listed.stdout
described = subprocess.run([str(cli), 'describe', 'process.list'],
                           text=True, capture_output=True, timeout=8)
assert described.returncode == 0 and 'input' in described.stdout
bad = subprocess.run([str(cli), 'missing-command'], text=True, capture_output=True, timeout=8)
assert bad.returncode == 2 and not bad.stdout and bad.stderr
summary = subprocess.run([str(cli), 'invoke', 'system.summary', '{}'],
                         text=True, capture_output=True, timeout=8)
assert summary.returncode == 0 and 'process_count' in json.loads(summary.stdout)
rights = subprocess.run([str(cli), 'invoke', 'permissions.list', '{}'],
                        text=True, capture_output=True, timeout=8)
assert rights.returncode == 0
rights_by_name = {item['name']: item['allowed'] for item in json.loads(rights.stdout)['items']}
assert rights_by_name['system.summary'] and not rights_by_name['process.terminate']
assert not rights_by_name['process.details']
devices = subprocess.run([str(cli), 'invoke', 'system.devices', '{"limit":2}'],
                         text=True, capture_output=True, timeout=12)
device_page = json.loads(devices.stdout)
assert devices.returncode == 0 and device_page['total'] >= len(device_page['items'])
assert len(device_page['items']) <= 2
print('CLI help, discovery, schema, JSON and exit codes passed.')

def rpc(command, messages):
    for message in messages:
        message.setdefault('params', {})['_meta'] = {
            'io.modelcontextprotocol/protocolVersion': '2026-07-28',
            'io.modelcontextprotocol/clientCapabilities': {},
        }
    process = subprocess.Popen([str(cli), *command], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               text=True, bufsize=1, creationflags=subprocess.CREATE_NO_WINDOW)
    received = queue.Queue()
    threading.Thread(target=lambda: [received.put(line) for line in process.stdout], daemon=True).start()
    try:
        for message in messages:
            process.stdin.write(json.dumps(message) + '\n')
        process.stdin.flush()
        replies = {}
        for _ in messages:
            reply = json.loads(received.get(timeout=15))
            if 'id' in reply: replies[reply['id']] = reply
        return replies
    finally:
        process.terminate()
        process.wait(timeout=5)

def call(request_id, name, arguments):
    return {'jsonrpc': '2.0', 'id': request_id, 'method': 'tools/call',
            'params': {'name': name, 'arguments': arguments}}

discovery = rpc(['mcp'], [
    {'jsonrpc': '2.0', 'id': 1, 'method': 'server/discover', 'params': {}},
    {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list', 'params': {}},
    call(3, 'system.summary', {}),
    call(4, 'process.terminate', {'pid': 4, 'created': 1, 'confirm': True}),
])
assert 3 in discovery, f'Missing read reply; IDs: {sorted(discovery)}'
assert 'result' in discovery[1]
assert {'system.summary', 'system.devices', 'process.list', 'process.get', 'process.terminate'} <= {
    tool['name'] for tool in discovery[2]['result']['tools']}
assert discovery[3]['result'].get('isError') is not True
assert discovery[4]['result'].get('isError') is True
print('MCP discovery, read and default refusal passed.')

def start_child():
    return subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)

def sample(child):
    for _ in range(30):
        result = subprocess.run([str(cli), 'invoke', 'process.list',
            json.dumps({'query': str(child.pid), 'limit': 100})], text=True,
            capture_output=True, timeout=8)
        if result.returncode == 0:
            rows = json.loads(result.stdout)['items']
            for row in rows:
                if row['pid'] == child.pid:
                    return row['created']
        time.sleep(0.1)
    raise AssertionError('Test child did not appear in process snapshot')

child = start_child()
try:
    created = sample(child)
    args = {'pid': child.pid, 'created': created, 'confirm': True}
    detail_args = json.dumps({'pid': child.pid, 'created': created, 'length': 128})
    details_denied = subprocess.run([str(cli), 'invoke', 'process.details', detail_args],
                                    text=True, capture_output=True, timeout=8)
    assert details_denied.returncode != 0
    details_allowed = subprocess.run([str(cli), 'invoke', 'process.details', detail_args,
                                      '--allow-details'], text=True, encoding='utf-8',
                                     capture_output=True, timeout=10)
    detail_page = json.loads(details_allowed.stdout)
    assert details_allowed.returncode == 0 and len(detail_page['text'].encode()) <= 132
    assert detail_page['total_bytes'] >= len(detail_page['text'].encode())
    print('Advanced report rights and bounded page passed.')
    denied = subprocess.run([str(cli), 'invoke', 'process.terminate', json.dumps(args)],
                            text=True, capture_output=True, timeout=8)
    assert denied.returncode != 0 and child.poll() is None
    args['confirm'] = False
    unconfirmed = subprocess.run([str(cli), 'invoke', 'process.terminate', json.dumps(args), '--allow-stop'],
                                 text=True, capture_output=True, timeout=8)
    assert unconfirmed.returncode != 0 and child.poll() is None
    args['confirm'] = True
    allowed = subprocess.run([str(cli), 'invoke', 'process.terminate', json.dumps(args), '--allow-stop'],
                             text=True, capture_output=True, timeout=8)
    if allowed.returncode:
        print('CLI stop error code:', json.loads(allowed.stderr)['error']['code'])
    assert allowed.returncode == 0 and json.loads(allowed.stdout)['accepted']
    assert child.wait(timeout=5) is not None
    print('CLI permission, confirmation and owned-process stop passed.')
finally:
    if child.poll() is None: child.terminate()

def http_call(url, message, token=None):
    message.setdefault('params', {})['_meta'] = {
        'io.modelcontextprotocol/protocolVersion': '2026-07-28',
        'io.modelcontextprotocol/clientCapabilities': {},
    }
    headers = {'Content-Type': 'application/json',
               'Accept': 'application/json, text/event-stream',
               'MCP-Protocol-Version': '2026-07-28',
               'Mcp-Method': message['method']}
    if message['method'] == 'tools/call': headers['Mcp-Name'] = message['params']['name']
    if token: headers['Authorization'] = 'Bearer ' + token
    with urllib.request.urlopen(urllib.request.Request(url,
        json.dumps(message).encode(), headers), timeout=10) as response:
        return json.load(response)

def result(reply):
    value = reply['result']
    assert not value.get('isError', False), value
    if 'structuredContent' in value: return value['structuredContent']
    return json.loads(value['content'][0]['text'])

def start_http(profile=None, token=None):
    env = dict(os.environ)
    env.pop('CYWINTASK_MCP_TOKEN', None)
    if token: env['CYWINTASK_MCP_TOKEN'] = token
    args = [str(cli), 'serve', '0']
    if profile: args += ['--profile', profile]
    process = subprocess.Popen(args, stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, env=env,
        creationflags=subprocess.CREATE_NO_WINDOW)
    line = process.stderr.readline()
    assert line.startswith('MCP listening at http://127.0.0.1:'), line[:120]
    return process, line.removeprefix('MCP listening at ').strip(), env

server, url, _ = start_http()
try:
    default_rights = result(http_call(url, call(19, 'permissions.list', {})))
    assert not all(item['allowed'] for item in default_rights['items']), 'Default serve profile is not controlled'
    assert http_call(url, call(18, 'process.terminate',
        {'pid': 4, 'created': 1, 'confirm': True}))['result']['isError']
    print('Default HTTP MCP profile is Local controlled.')
finally:
    server.terminate()
    server.wait(timeout=5)

server, url, env = start_http('free')
try:
    assert 'result' in http_call(url, {'jsonrpc': '2.0', 'id': 20,
        'method': 'server/discover', 'params': {}})
    assert 'process_count' in result(http_call(url, call(21, 'system.summary', {})))
    free_rights = result(http_call(url, call(22, 'permissions.list', {})))
    assert all(item['allowed'] for item in free_rights['items'])
    child = start_child()
    try:
        created = None
        for _ in range(50):
            rows = result(http_call(url, call(32, 'process.list',
                {'query': str(child.pid), 'limit': 100})))['items']
            created = next((row['created'] for row in rows if row['pid'] == child.pid), None)
            if created: break
            time.sleep(0.1)
        assert created, 'Owned test process did not appear in Free server snapshot'
        stopped = result(http_call(url, call(23, 'process.terminate',
            {'pid': child.pid, 'created': created})))
        assert stopped['accepted'] and child.wait(timeout=5) is not None
    finally:
        if child.poll() is None: child.terminate()
    print('Free HTTP MCP read and owned-process mutation without token passed.')
    remote = subprocess.run([str(cli), 'remote', url, 'list'], env=env,
                            text=True, capture_output=True, timeout=8)
    assert remote.returncode == 0 and 'system.summary' in remote.stdout
    remote = subprocess.run([str(cli), 'remote', url, 'describe', 'system.summary'], env=env,
                            text=True, capture_output=True, timeout=8)
    assert remote.returncode == 0 and 'inputSchema' in remote.stdout
    remote = subprocess.run([str(cli), 'remote', url, 'invoke', 'system.summary', '{}'], env=env,
                            text=True, capture_output=True, timeout=8)
    assert remote.returncode == 0 and not json.loads(remote.stdout).get('isError', False)
    print('CLI remote connection without token passed.')
finally:
    server.terminate()
    server.wait(timeout=5)

server, url, _ = start_http('local_controlled')
try:
    assert 'process_count' in result(http_call(url, call(24, 'system.summary', {})))
    assert http_call(url, call(25, 'process.terminate',
        {'pid': 4, 'created': 1, 'confirm': True}))['result']['isError']
    print('Local controlled address-only read and mutation denial passed.')
finally:
    server.terminate()
    server.wait(timeout=5)

token = secrets.token_urlsafe(36)
server, url, _ = start_http('protected', token)
try:
    try:
        http_call(url, {'jsonrpc': '2.0', 'id': 26, 'method': 'server/discover', 'params': {}})
        raise AssertionError('Protected profile accepted a missing token')
    except urllib.error.HTTPError as error:
        assert error.code == 401
    assert 'process_count' in result(http_call(url, call(27, 'system.summary', {}), token))
    assert http_call(url, call(28, 'process.terminate',
        {'pid': 4, 'created': 1, 'confirm': True}), token)['result']['isError']
    print('Protected HTTP MCP requires token and keeps operation permissions.')
finally:
    server.terminate()
    server.wait(timeout=5)

server, url, _ = start_http('token_only', token)
try:
    try:
        http_call(url, {'jsonrpc': '2.0', 'id': 29, 'method': 'server/discover', 'params': {}})
        raise AssertionError('Token-only profile accepted a missing token')
    except urllib.error.HTTPError as error:
        assert error.code == 401
    assert 'process_count' in result(http_call(url, call(30, 'system.summary', {}), token))
    assert all(item['allowed'] for item in result(http_call(url, call(31, 'permissions.list', {}), token))['items'])
    print('Token-only HTTP MCP requires token and permits registered operations.')
finally:
    server.terminate()
    server.wait(timeout=5)

child = start_child()
try:
    created = sample(child)
    allowed = rpc(['mcp', '--allow-stop'], [
        call(5, 'process.terminate', {'pid': child.pid, 'created': created, 'confirm': True})
    ])
    assert allowed[5]['result'].get('isError') is not True
    assert child.wait(timeout=5) is not None
    print('MCP authorized mutation passed.')
finally:
    if child.poll() is None: child.terminate()
