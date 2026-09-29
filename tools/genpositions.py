#!/usr/bin/env python3
"""Generate a corpus of quiet positions labelled with game results, for texeltune.

    STOCKFISH=/path/to/stockfish tools/genpositions.py OUTPUT [--games N] [--jobs J]

Games are played by one UCI engine against itself with random openings and
unequal, node-limited strength, so that they end in wins, draws and losses. Every
few plies of a game, when the side to move is not in check and the last move was
neither a capture nor a promotion, the position is written as `FEN | result`,
with the result from White's point of view (1, 0.5 or 0). The corpus is not kept
in the repository; the engine path comes from $STOCKFISH.
"""
import argparse
import multiprocessing
import os
import random
import re
import subprocess
import sys

START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


class Engine:
    def __init__(self, path, skill):
        self.proc = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)
        self.send("uci")
        self.wait("uciok")
        self.send(f"setoption name Skill Level value {skill}")
        self.send("setoption name Hash value 16")
        self.send("isready")
        self.wait("readyok")

    def send(self, text):
        self.proc.stdin.write(text + "\n")
        self.proc.stdin.flush()

    def wait(self, token):
        for line in self.proc.stdout:
            if line.startswith(token):
                return line

    def legal_moves(self, moves):
        self.send("position startpos moves " + " ".join(moves) if moves else "position startpos")
        self.send("go perft 1")
        found = []
        for line in self.proc.stdout:
            m = re.match(r"^([a-h][1-8][a-h][1-8][qrbn]?): 1", line)
            if m:
                found.append(m.group(1))
            if line.startswith("Nodes searched"):
                break
        return found

    def state(self, moves):
        """FEN of the position, and whether the side to move is in check."""
        self.send("position startpos moves " + " ".join(moves) if moves else "position startpos")
        self.send("d")
        fen, checkers = None, ""
        for line in self.proc.stdout:
            if line.startswith("Fen:"):
                fen = line.split(":", 1)[1].strip()
            elif line.startswith("Checkers:"):
                checkers = line.split(":", 1)[1].strip()
                break
        return fen, bool(checkers)

    def best(self, moves, nodes):
        self.send("position startpos moves " + " ".join(moves) if moves else "position startpos")
        self.send(f"go nodes {nodes}")
        for line in self.proc.stdout:
            if line.startswith("bestmove"):
                move = line.split()[1]
                return None if move == "(none)" else move

    def close(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def material_dead(fen):
    board = fen.split()[0]
    pieces = [c for c in board if c.isalpha() and c.lower() != "k"]
    if not pieces:
        return True
    return len(pieces) == 1 and pieces[0].lower() in "nb"


def play(engine, rng):
    moves = []
    for _ in range(rng.randint(6, 12)):
        legal = engine.legal_moves(moves)
        if not legal:
            return None
        moves.append(rng.choice(legal))
    strong, weak = rng.choice([(3000, 700), (2000, 1000), (1500, 1500)])
    white_nodes, black_nodes = (strong, weak) if rng.random() < 0.5 else (weak, strong)
    positions = []
    seen = {}
    result = None
    previous_pieces = None
    while result is None:
        fen, in_check = engine.state(moves)
        board = fen.split()[0]
        pieces = sum(c.isalpha() for c in board)
        fields = fen.split()
        key = " ".join(fields[:4])
        seen[key] = seen.get(key, 0) + 1
        white_to_move = fields[1] == "w"
        last = moves[-1] if moves else ""
        was_capture = previous_pieces is not None and pieces < previous_pieces
        previous_pieces = pieces
        if len(moves) >= 12 and not in_check and not was_capture and len(last) == 4 and rng.random() < 0.35:
            positions.append(fen)
        move = engine.best(moves, white_nodes if white_to_move else black_nodes)
        if move is None:
            result = (0.0 if white_to_move else 1.0) if in_check else 0.5
        elif seen[key] >= 3 or int(fields[4]) >= 100 or material_dead(fen) or len(moves) >= 260:
            result = 0.5
        else:
            moves.append(move)
    return [(fen, result) for fen in positions]


def worker(args):
    path, games, seed, index = args
    rng = random.Random(seed + index)
    engine = Engine(path, rng.randint(6, 12))
    rows = []
    for _ in range(games):
        game = play(engine, rng)
        if game:
            rows.extend(game)
    engine.close()
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output")
    parser.add_argument("--games", type=int, default=2000)
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    path = os.environ.get("STOCKFISH")
    if not path:
        sys.exit("set $STOCKFISH to the engine binary")
    per_job = max(1, args.games // args.jobs)
    with multiprocessing.Pool(args.jobs) as pool:
        chunks = pool.map(worker, [(path, per_job, args.seed, i) for i in range(args.jobs)])
    count = 0
    with open(args.output, "w") as out:
        for rows in chunks:
            for fen, result in rows:
                out.write(f"{fen} | {result:g}\n")
                count += 1
    print(f"{count} positions from {per_job * args.jobs} games")


if __name__ == "__main__":
    main()
