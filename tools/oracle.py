#!/usr/bin/env python3
"""Score positions with a UCI engine, from White's point of view.

    STOCKFISH=/path/to/stockfish tools/oracle.py [--depth N] < positions.txt

Input: one "label | FEN" per line (a bare FEN also works); blank lines and lines
starting with '#' are skipped. Output: "label | score | FEN" where the score is
in centipawns, or "#N" / "-#N" for a mate in N moves. The engine path comes from
$STOCKFISH and is never stored in the repository.
"""
import argparse
import os
import subprocess
import sys


def legal_enough(fen):
    """Reject what would hang or crash the engine: bad field count, no kings or
    adjacent kings (Stockfish loops on the latter)."""
    parts = fen.split()
    if len(parts) < 4:
        return False
    rows = parts[0].split("/")
    if len(rows) != 8:
        return False
    kings = {}
    for r, row in enumerate(rows):
        c = 0
        for ch in row:
            if ch.isdigit():
                c += int(ch)
            else:
                if ch in "kK":
                    kings.setdefault(ch, []).append((r, c))
                c += 1
        if c != 8:
            return False
    if len(kings.get("k", [])) != 1 or len(kings.get("K", [])) != 1:
        return False
    (wr, wc), (br, bc) = kings["K"][0], kings["k"][0]
    return max(abs(wr - br), abs(wc - bc)) > 1


class Engine:
    def __init__(self, path):
        self.proc = subprocess.Popen(
            [path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1)
        self.send("uci")
        self.wait_for("uciok")
        self.send("isready")
        self.wait_for("readyok")

    def send(self, text):
        self.proc.stdin.write(text + "\n")
        self.proc.stdin.flush()

    def wait_for(self, token):
        for line in self.proc.stdout:
            if line.startswith(token):
                return

    def score(self, fen, depth):
        self.send("ucinewgame")
        self.send(f"position fen {fen}")
        self.send(f"go depth {depth}")
        last = None
        for line in self.proc.stdout:
            if line.startswith("info") and " score " in line and "lowerbound" not in line \
                    and "upperbound" not in line:
                tokens = line.split()
                i = tokens.index("score")
                last = (tokens[i + 1], int(tokens[i + 2]))
            elif line.startswith("bestmove"):
                break
        white_to_move = fen.split()[1] == "w"
        if last is None:
            # No info line: the side to move is mated or stalemated.
            return "0"
        kind, value = last
        if not white_to_move:
            value = -value
        if kind == "mate":
            return ("#%d" % value) if value > 0 else ("-#%d" % -value)
        return "%+d" % value

    def close(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--depth", type=int, default=18)
    args = parser.parse_args()

    path = os.environ.get("STOCKFISH")
    if not path:
        sys.exit("set $STOCKFISH to the engine binary")
    engine = Engine(path)
    try:
        for line in sys.stdin:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            label, _, fen = line.rpartition("|")
            label, fen = label.strip(), fen.strip()
            if not legal_enough(fen):
                print(f"{label} | illegal | {fen}")
                continue
            print(f"{label} | {engine.score(fen, args.depth)} | {fen}", flush=True)
    finally:
        engine.close()


if __name__ == "__main__":
    main()
