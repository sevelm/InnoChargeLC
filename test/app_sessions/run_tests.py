"""Hardware-free regressions for the production app session reader and projection."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default=shutil.which('g++'))
args = parser.parse_args()
if not args.compiler:
    parser.error('Pass --compiler with the path to a native g++ compiler')


def extract(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


source = (root / 'src/charge_session_log.cpp').read_text(encoding='utf-8')
web = (root / 'src/A_Task_Web.cpp').read_text(encoding='utf-8')
header = (root / 'include/charge_session_log.hpp').read_text(encoding='utf-8')
parts = [re.search(r'enum class session_ocpp_state_t[^;]+;', header).group(0)]
for name in ['struct session_backend_t', 'struct stored_ocpp_link_t', 'struct charge_session_record_t']:
    parts.append(extract(source, name) + ';')
output = root / '.pio/app-sessions-tests'
output.mkdir(parents=True, exist_ok=True)
(output / 'records.hpp').write_text('\n'.join(parts), encoding='utf-8')
# Only Arduino String::isEmpty is adapted to the equivalent std::string::empty.
(output / 'reader.hpp').write_text('\n'.join([
    extract(source, 'static void addRecordToJson(').replace('.isEmpty()', '.empty()'),
    extract(source, 'String charge_session_log_to_json_page('),
    extract(web, 'static bool websocket_public_app_allowed('),
]), encoding='utf-8')
(output / 'handler.hpp').write_text(extract(web, 'if (strcmp(action, "requestAppSessions") == 0)'), encoding='utf-8')
environment = dict(os.environ)
environment['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + environment['PATH']
exe = output / ('app_sessions.exe' if os.name == 'nt' else 'app_sessions')
command = [args.compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations']
if os.name == 'nt':
    command.append('-static')
command += ['-I.pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src', '-I' + str(output),
            str(Path(__file__).parent / 'test_main.cpp'), '-o', str(exe)]
subprocess.run(command, cwd=root, env=environment, check=True)
subprocess.run([str(exe)], cwd=root, env=environment, check=True)
