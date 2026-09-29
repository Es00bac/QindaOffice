# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real Calc executable and CLI/MCP in an isolated offscreen session."""
import csv
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
    with tempfile.TemporaryDirectory(prefix='qindacalc-agent-') as root:
        root = Path(root)
        environment = os.environ.copy()
        for variable, name in [('XDG_RUNTIME_DIR', 'runtime'), ('XDG_CONFIG_HOME', 'config'),
                               ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache'),
                               ('XDG_DATA_HOME', 'data')]:
            directory = root / name
            directory.mkdir(mode=0o700)
            environment[variable] = str(directory)
        environment.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                           QQO_NO_SECRET_SERVICE='1',
                           DBUS_SESSION_BUS_ADDRESS=f'unix:path={root}/no-session-bus')
        rows = [['', '', '', ''] for _ in range(11)]
        rows[4] = ['Lighting', '1', '2250', '=B5*C5']
        rows[5][3] = '5382'
        rows[10][3] = '=SUM(D5:D10)'
        document = root / 'budget.csv'
        with document.open('w', newline='') as stream:
            csv.writer(stream).writerows(rows)
        command = [binary, '--new-instance', str(document)]
        if grant:
            command.insert(1, f'--agent-{grant}')
        with (root / 'application.log').open('w+') as log:
            application = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
            try:
                endpoint = root / 'runtime' / 'qindatk-agent' / f'qindacalc-{application.pid}.sock'

                def cli(*arguments):
                    result = subprocess.run([sys.executable, client, '--socket', str(endpoint), *arguments],
                                            env=environment, capture_output=True, text=True, timeout=25)
                    check(bool(result.stdout), f'CLI returned no JSON: {result.stderr}')
                    return result.returncode, json.loads(result.stdout)

                deadline = time.monotonic() + 25
                documents = []
                while time.monotonic() < deadline:
                    check(application.poll() is None, 'Calc exited during startup')
                    if endpoint.exists():
                        _, discovered = cli('documents')
                        documents = discovered['documents']
                        if not grant or documents:
                            break
                    time.sleep(0.05)
                check(endpoint.exists(), 'Calc did not publish an agent endpoint')
                if not grant:
                    check(not documents, 'A default launch exposed an ungranted document')
                    return
                check(len(documents) == 1, 'Expected exactly one granted document')
                doc_id = documents[0]['document_id']
                while time.monotonic() < deadline:
                    _, snapshot = cli('inspect', doc_id)
                    if not snapshot['loading'] and snapshot['cells'].get('D11', {}).get('value') == 7632:
                        break
                    time.sleep(0.05)
                check(snapshot['cells']['D11']['value'] == 7632, 'Loaded formula was not recalculated')
                arguments = ['invoke', doc_id, 'calc.set_cells', '--args', '{"cells":{"B5":"2"}}',
                             '--if-revision', snapshot['revision'], '--request-id', 'process-lighting']
                code, receipt = cli(*arguments)
                if grant == 'read':
                    check(code == 1 and receipt['status'] == 'read_only', 'Read grant permitted a mutation')
                    _, current = cli('inspect', doc_id)
                    check(current['cells']['D11']['value'] == 7632, 'Refusal changed the workbook')
                    return
                check(code == 0 and receipt['status'] == 'completed', 'Edit did not complete')
                check(receipt['result']['cells']['D11']['value'] == 9882, 'Receipt did not reflect recalculation')
                _, replay = cli(*arguments)
                check(replay.get('replayed') is True, 'Retry was not recognized')
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
                check(mcp.returncode == 0 and len(responses) == 2, 'MCP framing/initialization failed')
                check(responses[1]['result']['structuredContent']['cells']['D11']['value'] == 9882,
                      'MCP did not read the live application')
                _, current = cli('inspect', doc_id)
                code, undone = cli('invoke', doc_id, 'calc.undo', '--args',
                                   json.dumps({'undo_ref': receipt['undo_ref']}),
                                   '--if-revision', current['revision'], '--request-id', 'process-undo')
                check(code == 0 and undone['status'] == 'undone', 'Named edit was not undoable')
                check(undone['result']['cells']['D11']['value'] == 7632, 'Undo did not restore the formula result')
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
        print(f'PASS: actual Calc process, access={mode or "off"}')
