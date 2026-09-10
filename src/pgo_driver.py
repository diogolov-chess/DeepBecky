"""PGO workload driver. Wait for each bestmove; fail on rejection/crash/timeout."""
import argparse
import queue
import subprocess
import threading
from pathlib import Path

POSITIONS = [
    "position startpos",
    "position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5",
    "position startpos moves d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5",
    "position fen r1bqk2r/pppp1ppp/2n5/1B2p3/4n3/5N2/PPPP1PPP/RNBQ1RK1 w kq -",
    "position fen r1bqkb1r/pp2pppp/2np1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq -",
    "position fen rnbqkb1r/pp3ppp/4pn2/2pp4/2PP4/2N1PN2/PP3PPP/R1BQKB1R b KQkq -",
    "position fen r2q1rk1/1pp1bppp/p1np1nb1/4p3/2B1P1P1/2NP1N1P/PPP2P2/R1BQR1K1 w - -",
    "position fen r1b1qrk1/ppp2pbp/3p1np1/4p3/2PPP3/2N1BN2/PP2BPPP/R2Q1RK1 w - -",
    "position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
    "position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -",
    "position fen 8/8/4kpp1/3p1b2/p6P/2B5/6PK/8 w - -",
    "position fen 8/5k2/1p6/p1p2Pp1/P1P3K1/8/8/8 b - -",
    "position fen 2r3k1/1p3ppp/3bpn2/3p4/3P4/1PN1P3/1B3PPP/5RK1 w - -",
    "position fen 3r2k1/p4p1p/1p4p1/2r5/4B3/P3P1P1/1P1n1P1P/R2R2K1 b - -",
    "position fen 1r5k/P7/8/3pP3/8/8/8/7K w - d6"
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    parser.add_argument("--depth", type=int, default=12)
    args = parser.parse_args()
    executable = Path(args.engine).resolve()
    process = subprocess.Popen([str(executable)], cwd=executable.parent,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1)
    lines = queue.Queue()
    def reader():
        for line in process.stdout:
            lines.put(line.strip())
        lines.put(None)
    threading.Thread(target=reader, daemon=True).start()
    def send(command):
        process.stdin.write(command + "\n")
        process.stdin.flush()
    def wait(prefix):
        import time
        deadline = time.monotonic() + 180
        while True:
            line = lines.get(timeout=max(0.001, deadline - time.monotonic()))
            if line is None:
                raise RuntimeError("Engine exited before " + prefix)
            if "Invalid FEN" in line or "illegal move" in line or "ERROR:" in line:
                raise RuntimeError(line)
            if line.startswith(prefix):
                return line
            if time.monotonic() >= deadline:
                raise TimeoutError(prefix)
    try:
        send("uci")
        wait("uciok")
        # Deterministic profile: SMP/ponder are tested separately, not profiled
        # with scheduler-dependent races between workers.
        send("setoption name Threads value 1")
        send("isready")
        wait("readyok")
        send("ucinewgame")
        for index, position in enumerate(POSITIONS):
            send(position)
            send(f"go depth {args.depth}")
            result = wait("bestmove ")
            if result.split()[1] == "0000":
                raise RuntimeError("Unexpected terminal PGO position: " + position)
            print(f"PGO {index+1}/{len(POSITIONS)}: {result}", flush=True)
        send("quit")
        process.wait(timeout=10)
        if process.returncode:
            raise RuntimeError(f"Engine exit code {process.returncode}")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
if __name__ == "__main__":
    main()

