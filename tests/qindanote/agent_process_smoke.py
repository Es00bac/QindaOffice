# SPDX-License-Identifier: GPL-3.0-or-later
"""Read and edit the real, offscreen QindaNote process through CLI and MCP."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def exercise(binary, client, grant):
    with tempfile.TemporaryDirectory(prefix='qindanote-agent-') as root:
        root = Path(root)
        environment = os.environ.copy()
        for variable, name in [('XDG_RUNTIME_DIR', 'runtime'), ('XDG_CONFIG_HOME', 'config'),
                               ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache'),
                               ('XDG_DATA_HOME', 'data')]:
            directory = root / name
            directory.mkdir(mode=0o700)
            environment[variable] = str(directory)
        environment.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                           QQO_NO_SECRET_SERVICE='1', QQO_NO_HANDOFF='1',
                           DBUS_SESSION_BUS_ADDRESS=f'unix:path={root}/no-session-bus')
        notebook = root / 'debugging.qnote'
        pages = notebook / 'sections' / 'technical' / 'pages'
        pages.mkdir(parents=True)
        (notebook / 'notebook.json').write_text(json.dumps({
            'name': 'Technical notebook', 'sections': [{'id': 'technical', 'name': 'Technical',
                'pages': [{'id': 'driver-review', 'title': 'Driver review'}]}]}))
        blocks = [{'type': 'markdown', 'uid': 'session-1', 'text': '# Audio\n- reboot failed',
                   'transcript': '[02:15] Speaker 1: firmware 2.3 restored audio; cause unknown',
                   'session_started_at': '2026-09-14T15:00:00Z'},
                  {'type': 'check', 'text': 'Verify after suspend', 'checked': False}]
        (pages / 'driver-review.json').write_text(json.dumps({
            'id': 'driver-review', 'title': 'Driver review', 'background': 'blank',
            'ink': [], 'blocks': {'blocks': blocks}}))
        command = [binary, '--new-instance', str(notebook)]
        if grant:
            command.insert(1, f'--agent-{grant}')
        with (root / 'application.log').open('w+') as log:
            application = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
            try:
                endpoint = root / 'runtime' / 'qindatk-agent' / f'qindanote-{application.pid}.sock'

                def cli(*arguments):
                    result = subprocess.run([sys.executable, client, '--socket', str(endpoint), *arguments],
                                            env=environment, capture_output=True, text=True, timeout=25)
                    check(bool(result.stdout), f'CLI returned no JSON: {result.stderr}')
                    return result.returncode, json.loads(result.stdout)

                deadline = time.monotonic() + 25
                documents = []
                while time.monotonic() < deadline:
                    check(application.poll() is None, 'QindaNote exited during startup')
                    if endpoint.exists():
                        _, discovered = cli('documents')
                        documents = discovered['documents']
                        if not grant or documents:
                            break
                    time.sleep(0.05)
                check(endpoint.exists(), 'QindaNote did not publish an agent endpoint')
                if not grant:
                    check(not documents, 'A normal launch exposed an ungranted notebook')
                    return
                check(len(documents) == 1, 'Expected one granted notebook')
                doc_id = documents[0]['document_id']
                while time.monotonic() < deadline:
                    _, snapshot = cli('inspect', doc_id)
                    if snapshot.get('page_id') == 'driver-review' and len(snapshot.get('blocks', [])) == 2:
                        break
                    time.sleep(0.05)
                check('firmware 2.3' in snapshot['blocks'][0]['transcript'], 'Transcript not exposed')
                check(snapshot['view'].get('zoom', 0) > 0, 'Actual viewport was not exposed')
                edits = [{'op': 'set_checked', 'block': 1, 'checked': True},
                         {'op': 'rename_page', 'title': 'Verified audio'}]
                arguments = ['invoke', doc_id, 'note.edit', '--args', json.dumps({'edits': edits}),
                             '--if-revision', snapshot['revision'], '--request-id', 'review-1']
                code, receipt = cli(*arguments)
                if grant == 'read':
                    check(code == 1 and receipt['status'] == 'read_only', 'Read grant permitted a write')
                    _, current = cli('inspect', doc_id)
                    check(current['page_title'] == 'Driver review', 'Refusal changed the title')
                    check(not current['blocks'][1]['checked'], 'Refusal changed the checklist')
                    return
                check(code == 0 and receipt['status'] == 'completed', 'Edit did not complete')
                check(receipt['result']['page_title'] == 'Verified audio', 'Title edit missing from receipt')
                check(receipt['result']['blocks'][1]['checked'], 'Checklist did not change')
                _, replay = cli(*arguments)
                check(replay.get('replayed') is True, 'Delivery retry was applied again')
                requests = [
                    {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize'},
                    {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
                    {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
                     'params': {'name': 'qinda_inspect', 'arguments': {'document_id': doc_id}}},
                ]
                mcp = subprocess.run([sys.executable, client, '--socket', str(endpoint), 'mcp'],
                                     input=''.join(json.dumps(r) + '\n' for r in requests),
                                     env=environment, capture_output=True, text=True, timeout=25)
                responses = [json.loads(line) for line in mcp.stdout.splitlines()]
                check(mcp.returncode == 0 and len(responses) == 2, 'MCP initialization/framing failed')
                state = responses[1]['result']['structuredContent']
                check(state['page_title'] == 'Verified audio', 'MCP did not read the live page')
                _, current = cli('inspect', doc_id)
                code, invalid = cli('invoke', doc_id, 'note.edit', '--args', json.dumps({'edits': [
                    {'op': 'rename_page', 'title': 'Must not land'}, {'op': 'delete_block', 'block': 99}]}),
                    '--if-revision', current['revision'], '--request-id', 'bad-batch')
                check(code == 1 and invalid['status'] == 'validation_failed', 'Invalid batch was accepted')
                _, current = cli('inspect', doc_id)
                check(current['page_title'] == 'Verified audio', 'Invalid batch partially changed the title')
                code, undone = cli('invoke', doc_id, 'note.undo', '--args',
                                   json.dumps({'undo_ref': receipt['undo_ref']}),
                                   '--if-revision', current['revision'], '--request-id', 'review-undo')
                check(code == 0 and undone['status'] == 'undone', 'Named edit was not undoable')
                check(undone['result']['page_title'] == 'Driver review', 'Undo did not restore the title')
                check(not undone['result']['blocks'][1]['checked'], 'Undo did not restore the checklist')
                code, stale = cli('invoke', doc_id, 'note.edit', '--args', json.dumps({'edits': edits}),
                                  '--if-revision', snapshot['revision'], '--request-id', 'stale')
                check(code == 1 and stale['status'] == 'stale_revision', 'Edit/undo accepted an old revision')
            except Exception:
                log.seek(0)
                sys.stderr.write(log.read())
                raise
            finally:
                application.terminate()
                try:
                    application.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    application.kill()
                    application.wait(timeout=5)


if __name__ == '__main__':
    for mode in ('', 'read', 'edit'):
        exercise(sys.argv[1], sys.argv[2], mode)
        print(f'PASS: actual QindaNote process, access={mode or "off"}')
