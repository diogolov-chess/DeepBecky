"""Record a deterministic 1T bench signature in the binary manifest."""
import argparse
import hashlib
import json
import pathlib
import queue
import re
import subprocess
import threading
import time
from pgo_driver import POSITIONS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--engine', required=True, type=pathlib.Path)
    parser.add_argument('--depth', type=int, default=10)
    args = parser.parse_args()
    binary = args.engine.resolve()
    process = subprocess.Popen([str(binary)], cwd=binary.parent, text=True,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines = queue.Queue()
    def read():
        for line in process.stdout:
            lines.put(line.strip())
        lines.put(None)
    threading.Thread(target=read, daemon=True).start()
    def send(command):
        process.stdin.write(command+'\n'); process.stdin.flush()
    def wait(prefix):
        seen=[]; deadline=time.monotonic()+120
        while True:
            line=lines.get(timeout=max(.001, deadline-time.monotonic()))
            if line is None or 'ERROR:' in line:
                raise RuntimeError(str(line))
            seen.append(line)
            if line.startswith(prefix): return seen
    records=[]
    try:
        send('uci'); wait('uciok')
        send('setoption name Threads value 1'); send('setoption name Hash value 16')
        send('isready'); wait('readyok')
        for position in POSITIONS:
            send('ucinewgame'); send(position); send(f'go depth {args.depth}')
            output=wait('bestmove ')
            info=[line for line in output if re.match(r'info depth \d+ seldepth', line)][-1]
            fields=info.split()
            records.append(dict(position=position, depth=int(fields[2]),
                                nodes=int(fields[fields.index('nodes')+1]),
                                score=fields[fields.index('score')+1:fields.index('score')+3],
                                bestmove=output[-1].split()[1]))
        send('quit'); process.wait(timeout=10)
        if process.returncode: raise RuntimeError('nonzero engine exit')
    finally:
        if process.poll() is None: process.kill(); process.wait()
    signature=hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()
    manifest=pathlib.Path(str(binary)+'.manifest.json')
    data=json.loads(manifest.read_text())
    if data['binary']['sha256'] != hashlib.sha256(binary.read_bytes()).hexdigest():
        raise RuntimeError('Binary differs from manifest')
    data['benchmark']=dict(threads=1, hash_mb=16, depth=args.depth, signature_sha256=signature, records=records)
    manifest.write_text(json.dumps(data, sort_keys=True, indent=2), encoding='utf-8')
    print(f'Artifact bench: {len(records)} positions, {sum(r["nodes"] for r in records)} nodes, SHA256 {signature}')


if __name__ == '__main__':
    main()
