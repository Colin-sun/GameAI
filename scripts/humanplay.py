#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import json5
from rich.align import Align
from rich.console import Console, Group
from rich.panel import Panel
from rich.prompt import Prompt
from rich.text import Text


COL_LABELS = "ABCDEFGHI"
EMPTY = 0
PLAYER_ONE = 1
PLAYER_TWO = 2


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_native_module(module_dir: Path):
    try:
        import gameai_native  # pylint: disable=import-error

        return gameai_native
    except ModuleNotFoundError:
        pass

    if not module_dir.exists():
        raise FileNotFoundError(
            f"Python bindings not found at {module_dir}, and no installed gameai_native module was found. "
            "Run `python -m pip install .` or build with CMake manually."
        )

    sys.path.insert(0, str(module_dir))
    import gameai_native  # pylint: disable=import-error

    return gameai_native


def load_config(config_path: Path) -> dict:
    with config_path.open("r", encoding="utf-8") as handle:
        return json5.load(handle)


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Play Ultimate Tic Tac Toe against pure MCTS.")
    parser.add_argument(
        "--config",
        type=Path,
        default=root / "configs" / "humanplay" / "humanplay_config.json5",
        help="Path to the JSON5 config file.",
    )
    parser.add_argument(
        "--module-dir",
        type=Path,
        default=root / "build" / "native" / "python",
        help="Directory containing the compiled gameai_native module.",
    )
    parser.add_argument("--ai-player", type=int, choices=(1, 2), help="Override config AI side.")
    parser.add_argument("--n-playout", type=int, help="Override config MCTS playout count.")
    parser.add_argument("--c-puct", type=float, help="Override config exploration constant.")
    parser.add_argument("--seed", type=int, default=20260512, help="Seed for the persistent AI tree.")
    parser.add_argument("--no-color", action="store_true", help="Disable ANSI color output.")
    return parser.parse_args()


def merge_runtime_config(args: argparse.Namespace) -> dict:
    config = load_config(args.config)
    mcts_cfg = config.setdefault("mcts", {})

    if args.ai_player is not None:
        config["ai_player"] = args.ai_player
    if args.n_playout is not None:
        mcts_cfg["n_playout"] = args.n_playout
    if args.c_puct is not None:
        mcts_cfg["c_puct"] = args.c_puct

    if config.get("ai_player") not in (1, 2):
        raise ValueError("ai_player must be 1 or 2")
    if int(mcts_cfg.get("n_playout", 0)) <= 0:
        raise ValueError("mcts.n_playout must be > 0")

    return config


def action_to_label(action: int) -> str:
    row, col = divmod(action, 9)
    return f"{COL_LABELS[col]}{9 - row}"


def label_to_action(raw: str) -> int | None:
    value = raw.strip().upper()
    if not value:
        return None

    if len(value) >= 2 and value[0] in COL_LABELS and value[1:].isdigit():
        col = COL_LABELS.index(value[0])
        rank = int(value[1:])
        if 1 <= rank <= 9:
            row = 9 - rank
            return row * 9 + col

    if "," in value:
        row_text, col_text = [part.strip() for part in value.split(",", maxsplit=1)]
        if row_text.isdigit() and col_text.isdigit():
            row = int(row_text)
            col = int(col_text)
            if 1 <= row <= 9 and 1 <= col <= 9:
                return (9 - row) * 9 + (col - 1)

    return None


def player_name(player: int) -> str:
    return "Player 1" if player == PLAYER_ONE else "Player 2"


def player_style(player: int) -> str:
    return "bold red" if player == PLAYER_ONE else "bold cyan"


def symbol_for_cell(value: int) -> tuple[str, str]:
    if value == PLAYER_ONE:
        return "●", "bold red"
    if value == PLAYER_TWO:
        return "×", "bold cyan"
    return "·", "grey62"


def active_sub_board_cells(next_board: tuple[int, int], meta_board: list[list[int]]) -> set[tuple[int, int]]:
    sub_row, sub_col = next_board
    if sub_row == -1 or sub_col == -1:
        return set()
    if meta_board[sub_row][sub_col] != 0:
        return set()

    return {
        (sub_row * 3 + row_offset, sub_col * 3 + col_offset)
        for row_offset in range(3)
        for col_offset in range(3)
    }


def render_board(game, suggested_action: int | None) -> Text:
    board = game.get_board()
    meta_board = game.get_meta_board()
    next_board = tuple(game.get_next_board())
    active_cells = active_sub_board_cells(next_board, meta_board)
    valid_actions = set(game.get_valid_actions())

    lines = [Text("    A B C   D E F   G H I", style="bold white")]
    horizontal = Text("  ───────┼───────┼───────", style="bright_black")

    for row in range(9):
        line = Text(f" {9 - row} ", style="bold white")
        for col in range(9):
            cell_value = board[row][col]
            cell_action = row * 9 + col
            char, style = symbol_for_cell(cell_value)

            if cell_value == EMPTY:
                if suggested_action == cell_action:
                    char = "◎"
                    style = "bold magenta"
                elif (row, col) in active_cells:
                    style = "bold white on rgb(35,35,55)"
                elif cell_action in valid_actions:
                    style = "grey70"

            line.append(char, style)
            if col != 8:
                separator = " "
                separator_style = "white"
                if col in (2, 5):
                    separator = " │ "
                    separator_style = "bright_black"
                line.append(separator, separator_style)
        lines.append(line)
        if row in (2, 5):
            lines.append(horizontal.copy())

    return Text("\n").join(lines)


def render_meta_status(game) -> Text:
    meta_board = game.get_meta_board()
    rows: list[Text] = []
    for row in meta_board:
        line = Text()
        for value in row:
            if value == PLAYER_ONE:
                line.append("● ", "bold red")
            elif value == PLAYER_TWO:
                line.append("× ", "bold cyan")
            elif value == 3:
                line.append("■ ", "grey50")
            else:
                line.append("· ", "grey70")
        rows.append(line)
    return Text("\n").join(rows)


def build_status_panel(game, ai_player: int, last_move: int | None, suggested_action: int | None) -> Panel:
    done, winner = game.get_done_winner()
    current_player = game.get_current_player()
    next_board = tuple(game.get_next_board())

    status_lines: list[Text] = []
    status_lines.append(Text("Pure MCTS Human Play", style="bold bright_white"))
    status_lines.append(Text.assemble("AI side: ", (player_name(ai_player), player_style(ai_player))))
    status_lines.append(
        Text.assemble("Turn: ", (player_name(current_player), player_style(current_player)))
    )
    if next_board == (-1, -1):
        status_lines.append(Text("Target sub-board: any unfinished 3x3", style="white"))
    else:
        status_lines.append(
            Text(f"Target sub-board: ({next_board[0] + 1}, {next_board[1] + 1})", style="white")
        )
    status_lines.append(Text(f"Step: {game.get_step()}", style="white"))
    status_lines.append(
        Text(f"Last move: {action_to_label(last_move)}" if last_move is not None else "Last move: --")
    )

    if suggested_action is not None and not done and current_player != ai_player:
        status_lines.append(
            Text.assemble("Suggested move: ", (action_to_label(suggested_action), "bold magenta"))
        )

    if done:
        if winner == -1:
            status_lines.append(Text("Result: draw", style="bold yellow"))
        else:
            status_lines.append(
                Text.assemble("Result: ", (f"{player_name(winner)} wins", player_style(winner)))
            )

    status_lines.append(Text("Commands: A1 / hint / accept / quit", style="grey70"))
    status_lines.append(Text("Alternate input: row,col with 1-based board coordinates", style="grey70"))
    status_lines.append(Text("Meta board:", style="bold white"))
    status_lines.append(render_meta_status(game))

    return Panel(
        Group(*status_lines),
        title="Status",
        border_style="bright_magenta",
        padding=(1, 2),
    )


def build_screen(console: Console, game, ai_player: int, last_move: int | None, suggested_action: int | None) -> None:
    board_panel = Panel(
        Align.center(render_board(game, suggested_action)),
        title="Ultimate Tic Tac Toe",
        subtitle="◎ = suggested move",
        border_style="red",
        padding=(1, 2),
    )
    status_panel = build_status_panel(game, ai_player, last_move, suggested_action)
    screen = Group(
        Panel(
            Text("辅助走子 / Human Play", justify="center", style="bold white"),
            border_style="magenta",
            padding=(0, 2),
        ),
        board_panel,
        status_panel,
    )
    console.clear()
    console.print(screen)


def announce(console: Console, message: str, style: str = "bold yellow") -> None:
    console.print(Panel(Text(message, justify="center", style=style), border_style=style))


def main() -> int:
    args = parse_args()
    console = Console(no_color=args.no_color)
    config = merge_runtime_config(args)
    module = load_native_module(args.module_dir)

    ai_player = int(config["ai_player"])
    mcts_cfg = config["mcts"]
    n_playout = int(mcts_cfg["n_playout"])
    c_puct = float(mcts_cfg["c_puct"])

    game = module.UltimateTicTacToe()
    ai = module.MCTSPure(n_playout=n_playout, c_puct=c_puct, seed=args.seed)
    last_move: int | None = None
    suggested_action: int | None = None

    while True:
        done, winner = game.get_done_winner()
        current_player = game.get_current_player()

        if not done and current_player != ai_player and suggested_action is None:
            suggested_action = ai.suggest_move(game)

        build_screen(console, game, ai_player, last_move, suggested_action)

        if done:
            if winner == -1:
                announce(console, "Game over: draw", "bold yellow")
            else:
                announce(console, f"Game over: {player_name(winner)} wins", player_style(winner))
            return 0

        if current_player == ai_player:
            ai_move = ai.get_move(game)
            if ai_move == -1 or not game.make_move(ai_move):
                raise RuntimeError("AI produced an invalid move")
            last_move = ai_move
            suggested_action = None
            continue

        try:
            if suggested_action is not None:
                raw = Prompt.ask("[bold white]Your move[/bold white]", default="accept")
            else:
                raw = Prompt.ask("[bold white]Your move[/bold white]")
        except EOFError:
            announce(console, "EOF received, exiting", "bold white")
            return 0
        command = raw.strip().lower()

        if command in {"quit", "q", "exit"}:
            announce(console, "Session ended", "bold white")
            return 0
        if command in {"hint", "h"}:
            suggested_action = ai.suggest_move(game)
            continue
        if command in {"accept", "a"}:
            if suggested_action is None:
                announce(console, "No suggestion available yet.", "bold yellow")
                continue
            action = suggested_action
        else:
            action = label_to_action(raw)
            if action is None:
                announce(console, "Invalid input. Use A1, D5, 3,4, hint, accept, or quit.", "bold yellow")
                continue

        if not game.is_action_valid(action):
            announce(console, f"{action_to_label(action)} is not legal in the current sub-board.", "bold yellow")
            continue

        if not game.make_move(action):
            announce(console, "Move rejected by the engine.", "bold yellow")
            continue

        ai.update_with_move(action)
        last_move = action
        suggested_action = None


if __name__ == "__main__":
    raise SystemExit(main())
