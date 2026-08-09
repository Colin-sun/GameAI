#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.error import URLError
from urllib.request import urlopen
from urllib.parse import urlsplit

import json5


COL_LABELS = "ABCDEFGHI"
EMPTY = 0
PLAYER_ONE = 1
PLAYER_TWO = 2


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_native_module(module_dir: Path):
    if module_dir.exists():
        sys.path.insert(0, str(module_dir))
        try:
            import gameai_native  # pylint: disable=import-error

            return gameai_native
        except ModuleNotFoundError:
            sys.path.pop(0)

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


def default_pid_file() -> Path:
    repo_key = hashlib.sha256(str(repo_root()).encode("utf-8")).hexdigest()[:12]
    return Path(tempfile.gettempdir()) / f"gameai-humanplay-{repo_key}.json"


def add_runtime_arguments(parser: argparse.ArgumentParser) -> None:
    root = repo_root()
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
    parser.add_argument(
        "--rollout-policy",
        choices=("uniform", "tactical"),
        help="Override the rollout policy.",
    )
    parser.add_argument("--rollout-limit", type=int, help="Override rollout cutoff length.")
    parser.add_argument("--seed", type=int, default=20260512, help="Seed for the persistent AI tree.")
    parser.add_argument("--host", default="127.0.0.1", help="HTTP bind host.")
    parser.add_argument("--port", type=int, default=8765, help="HTTP bind port.")
    parser.add_argument(
        "--pid-file",
        type=Path,
        default=default_pid_file(),
        help="Path to the background server state file.",
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    raw_args = list(sys.argv[1:] if argv is None else argv)
    if raw_args and raw_args[0] == "_serve":
        serve_parser = argparse.ArgumentParser(description=argparse.SUPPRESS)
        add_runtime_arguments(serve_parser)
        args = serve_parser.parse_args(raw_args[1:])
        args.command = "_serve"
        return args

    parser = argparse.ArgumentParser(
        description="Start or stop the GameAI Ultimate Tic Tac Toe browser server."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    start_parser = subparsers.add_parser("start", help="Start the server in the background and exit.")
    add_runtime_arguments(start_parser)
    start_parser.add_argument("--no-browser", action="store_true", help="Do not open a browser automatically.")

    stop_parser = subparsers.add_parser("stop", help="Stop the background server.")
    stop_parser.add_argument(
        "--pid-file",
        type=Path,
        default=default_pid_file(),
        help="Path to the background server state file.",
    )

    return parser.parse_args(raw_args)


def merge_runtime_config(args: argparse.Namespace) -> dict:
    config = load_config(args.config)
    mcts_cfg = config.setdefault("mcts", {})

    if args.ai_player is not None:
        config["ai_player"] = args.ai_player
    if args.n_playout is not None:
        mcts_cfg["n_playout"] = args.n_playout
    if args.c_puct is not None:
        mcts_cfg["c_puct"] = args.c_puct
    if args.rollout_policy is not None:
        mcts_cfg["rollout_policy"] = args.rollout_policy
    if args.rollout_limit is not None:
        mcts_cfg["rollout_limit"] = args.rollout_limit

    if config.get("ai_player") not in (PLAYER_ONE, PLAYER_TWO):
        raise ValueError("ai_player must be 1 or 2")
    if int(mcts_cfg.get("n_playout", 0)) <= 0:
        raise ValueError("mcts.n_playout must be > 0")
    if float(mcts_cfg.get("c_puct", 0.0)) <= 0:
        raise ValueError("mcts.c_puct must be > 0")
    if mcts_cfg.get("rollout_policy", "tactical") not in ("uniform", "tactical", 0, 1):
        raise ValueError("mcts.rollout_policy must be uniform or tactical")
    if int(mcts_cfg.get("rollout_limit", 32)) <= 0:
        raise ValueError("mcts.rollout_limit must be > 0")

    return config


def action_to_label(action: int | None) -> str:
    if action is None or action < 0 or action >= 81:
        return "--"
    row, col = divmod(action, 9)
    return f"{COL_LABELS[col]}{9 - row}"


def player_name(player: int) -> str:
    return "Player 1" if player == PLAYER_ONE else "Player 2"


def rollout_policy_id(value: str | int) -> int:
    if value in ("uniform", 0):
        return 0
    if value in ("tactical", 1):
        return 1
    raise ValueError("mcts.rollout_policy must be uniform or tactical")


class APIError(Exception):
    def __init__(self, status: int | HTTPStatus, message: str) -> None:
        super().__init__(message)
        self.status = int(status)
        self.message = message


class GameSession:
    """One in-memory game and its persistent MCTS engine."""

    def __init__(
        self,
        module: Any,
        ai_player: int,
        n_playout: int,
        c_puct: float,
        rollout_policy: int,
        rollout_limit: int,
        seed: int,
    ) -> None:
        self.module = module
        self.ai_player = ai_player
        self.n_playout = n_playout
        self.c_puct = c_puct
        self.rollout_policy = rollout_policy
        self.rollout_limit = rollout_limit
        self.seed = seed
        self.lock = threading.RLock()
        self.game: Any = None
        self.ai: Any = None
        self.last_move: int | None = None
        self.last_move_player: int | None = None
        self.suggested_action: int | None = None
        self.searching = False
        self.search_error: str | None = None
        self.search_error_kind: str | None = None
        self.tree_history: list[dict[str, Any]] = []
        self.tree_snapshots: dict[int, dict[str, Any]] = {}
        self.next_tree_id = 1
        self._reset_locked()

    def _new_ai(self):
        return self.module.MCTSPure(
            n_playout=self.n_playout,
            c_puct=self.c_puct,
            seed=self.seed,
            capture_search_tree=True,
            rollout_policy=self.rollout_policy,
            rollout_limit=self.rollout_limit,
        )

    def _reset_locked(self) -> None:
        self.game = self.module.UltimateTicTacToe()
        self.ai = self._new_ai()
        self.last_move = None
        self.last_move_player = None
        self.suggested_action = None
        self.search_error = None
        self.search_error_kind = None
        self.tree_history.clear()
        self.tree_snapshots.clear()
        self.next_tree_id = 1

    def reset(self) -> dict[str, Any]:
        with self.lock:
            if self.searching:
                raise APIError(HTTPStatus.CONFLICT, "AI search is in progress")
            self._reset_locked()
        return self.state_payload()

    def _state_status_locked(self, done: bool) -> str:
        if self.searching:
            return "thinking"
        if self.search_error:
            return "error"
        if done:
            return "finished"
        if self.game.get_current_player() == self.ai_player:
            return "ai_turn"
        return "player_turn"

    def state_payload(self) -> dict[str, Any]:
        with self.lock:
            done, winner = self.game.get_done_winner()
            current_player = self.game.get_current_player()
            status = self._state_status_locked(done)
            if self.searching:
                message = "MCTS is evaluating the position."
            elif self.search_error:
                message = self.search_error
            elif done and winner == -1:
                message = "The board is settled as a draw."
            elif done:
                message = f"{player_name(winner)} wins the game."
            elif current_player == self.ai_player:
                message = "The AI is ready to search."
            else:
                message = "Choose a legal square."

            return {
                "board": self.game.get_board(),
                "meta_board": self.game.get_meta_board(),
                "next_board": list(self.game.get_next_board()),
                "legal_actions": list(self.game.get_valid_actions()),
                "current_player": current_player,
                "ai_player": self.ai_player,
                "step": self.game.get_step(),
                "last_move": self.last_move,
                "last_move_player": self.last_move_player,
                "suggested_action": self.suggested_action,
                "done": bool(done),
                "winner": winner,
                "status": status,
                "message": message,
                "searching": self.searching,
                "search_error": self.search_error,
                "search_error_kind": self.search_error_kind,
                "can_move": (
                    not self.searching
                    and not done
                    and (not self.search_error or self.search_error_kind == "hint")
                    and current_player != self.ai_player
                ),
                "config": {
                    "n_playout": self.n_playout,
                    "c_puct": self.c_puct,
                    "rollout_policy": self.rollout_policy,
                    "rollout_limit": self.rollout_limit,
                    "seed": self.seed,
                },
                "tree_history": [dict(item) for item in self.tree_history],
            }

    def move_player(self, action: int) -> dict[str, Any]:
        with self.lock:
            done, _ = self.game.get_done_winner()
            if self.searching:
                raise APIError(HTTPStatus.CONFLICT, "AI search is in progress")
            if done:
                raise APIError(HTTPStatus.CONFLICT, "The game is already finished")
            if self.game.get_current_player() == self.ai_player:
                raise APIError(HTTPStatus.CONFLICT, "It is the AI's turn")
            if not self.game.is_action_valid(action):
                raise APIError(HTTPStatus.BAD_REQUEST, f"{action_to_label(action)} is not a legal move")
            if not self.game.make_move(action):
                raise APIError(HTTPStatus.BAD_REQUEST, "The engine rejected the move")
            self.ai.update_with_move(action)
            self.last_move = action
            self.last_move_player = 3 - self.ai_player
            self.suggested_action = None
            self.search_error = None
            self.search_error_kind = None
        return self.state_payload()

    def run_ai_turn(self) -> dict[str, Any]:
        with self.lock:
            done, _ = self.game.get_done_winner()
            if self.searching:
                raise APIError(HTTPStatus.CONFLICT, "AI search is in progress")
            if done:
                raise APIError(HTTPStatus.CONFLICT, "The game is already finished")
            if self.game.get_current_player() != self.ai_player:
                raise APIError(HTTPStatus.CONFLICT, "It is the player's turn")
            game = self.game
            step_before = game.get_step()
            ai_player = game.get_current_player()
            self.searching = True
            self.search_error = None
            self.search_error_kind = None

        failure: Exception | None = None
        try:
            action = int(self.ai.get_move(game))
            tree = json.loads(json.dumps(self.ai.get_last_search_tree()))
            with self.lock:
                if action < 0 or not game.is_action_valid(action):
                    raise RuntimeError("MCTS produced an invalid move")
                if not game.make_move(action):
                    raise RuntimeError("The engine rejected the AI move")

                tree_id = self.next_tree_id
                self.next_tree_id += 1
                tree.update(
                    {
                        "id": tree_id,
                        "step": step_before + 1,
                        "player": ai_player,
                        "action": action,
                        "action_label": action_to_label(action),
                        "n_playout": self.n_playout,
                        "c_puct": self.c_puct,
                        "rollout_policy": self.rollout_policy,
                        "rollout_limit": self.rollout_limit,
                    }
                )
                self.tree_snapshots[tree_id] = tree
                self.tree_history.append(
                    {
                        "id": tree_id,
                        "step": step_before + 1,
                        "player": ai_player,
                        "action": action,
                        "action_label": action_to_label(action),
                        "selected_action": tree["selected_action"],
                        "root_visits": tree["root_visits"],
                        "node_count": tree["node_count"],
                    }
                )
                self.last_move = action
                self.last_move_player = ai_player
                self.suggested_action = None
                self.search_error = None
                self.search_error_kind = None
        except Exception as exc:  # Keep the page alive so the user can retry.
            failure = exc
            with self.lock:
                self.search_error = f"AI search failed: {exc}".strip()
                self.search_error_kind = "ai"
                self.ai = self._new_ai()
        finally:
            with self.lock:
                self.searching = False

        if failure is not None:
            raise APIError(HTTPStatus.INTERNAL_SERVER_ERROR, self.search_error or "AI search failed")
        return self.state_payload()

    def get_hint(self) -> dict[str, Any]:
        with self.lock:
            done, _ = self.game.get_done_winner()
            if self.searching:
                raise APIError(HTTPStatus.CONFLICT, "AI search is in progress")
            if done:
                raise APIError(HTTPStatus.CONFLICT, "The game is already finished")
            if self.game.get_current_player() == self.ai_player:
                raise APIError(HTTPStatus.CONFLICT, "Hints are available on the player's turn")
            game = self.game
            self.searching = True
            self.search_error = None
            self.search_error_kind = None

        failure: Exception | None = None
        try:
            action = int(self.ai.suggest_move(game))
            with self.lock:
                if action < 0 or not game.is_action_valid(action):
                    raise RuntimeError("MCTS produced an invalid hint")
                self.suggested_action = action
                self.search_error = None
                self.search_error_kind = None
        except Exception as exc:  # Keep the page alive so the user can retry.
            failure = exc
            with self.lock:
                self.search_error = f"Hint search failed: {exc}".strip()
                self.search_error_kind = "hint"
        finally:
            with self.lock:
                self.searching = False

        if failure is not None:
            raise APIError(HTTPStatus.INTERNAL_SERVER_ERROR, self.search_error or "Hint search failed")
        return self.state_payload()

    def tree_payload(self, tree_id: int) -> dict[str, Any]:
        with self.lock:
            if tree_id not in self.tree_snapshots:
                raise APIError(HTTPStatus.NOT_FOUND, f"Search tree {tree_id} was not found")
            return json.loads(json.dumps(self.tree_snapshots[tree_id]))


class HumanPlayHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], session: GameSession) -> None:
        self.session = session
        super().__init__(address, HumanPlayRequestHandler)


class HumanPlayServerController:
    """Own a browser server that can be started, stopped, and restarted safely."""

    def __init__(self, session: GameSession, host: str = "127.0.0.1", port: int = 8765) -> None:
        self.session = session
        self.host = host
        self.port = port
        self._lifecycle_lock = threading.RLock()
        self._server: HumanPlayHTTPServer | None = None
        self._thread: threading.Thread | None = None
        self._ready = threading.Event()
        self._thread_error: Exception | None = None

    @property
    def server(self) -> HumanPlayHTTPServer | None:
        with self._lifecycle_lock:
            return self._server

    @property
    def thread(self) -> threading.Thread | None:
        with self._lifecycle_lock:
            return self._thread

    @property
    def running(self) -> bool:
        with self._lifecycle_lock:
            return self._server is not None and self._thread is not None and self._thread.is_alive()

    @property
    def thread_error(self) -> Exception | None:
        with self._lifecycle_lock:
            return self._thread_error

    @property
    def address(self) -> tuple[str, int]:
        with self._lifecycle_lock:
            if self._server is None:
                raise RuntimeError("HTTP server is not started")
            host, port = self._server.server_address[:2]
            return host, port

    @property
    def url(self) -> str:
        host, port = self.address
        return f"http://{host}:{port}/"

    def _serve(self) -> None:
        server = self._server
        if server is None:
            return
        self._ready.set()
        try:
            server.serve_forever(poll_interval=0.2)
        except Exception as exc:  # Surface unexpected background failures to the owner.
            with self._lifecycle_lock:
                self._thread_error = exc

    def start(self) -> str:
        with self._lifecycle_lock:
            if self.running:
                return self.url
            if self._server is not None:
                self._server.server_close()
            self._server = HumanPlayHTTPServer((self.host, self.port), self.session)
            self._thread_error = None
            self._ready.clear()
            self._thread = threading.Thread(
                target=self._serve,
                name="gameai-humanplay-http",
                daemon=True,
            )
            self._thread.start()

        if not self._ready.wait(timeout=5.0):
            self.stop()
            raise RuntimeError("HTTP server background thread did not start")
        if not self.running:
            error = self.thread_error
            self.stop()
            raise RuntimeError("HTTP server background thread stopped during startup") from error
        return self.url

    def stop(self, timeout: float = 5.0) -> None:
        with self._lifecycle_lock:
            server = self._server
            thread = self._thread
            if server is None:
                return
            if thread is not None and thread.is_alive():
                server.shutdown()

        if thread is not None:
            thread.join(timeout=timeout)
            if thread.is_alive():
                raise RuntimeError("HTTP server background thread did not stop")

        with self._lifecycle_lock:
            server.server_close()
            self._server = None
            self._thread = None
            self._ready.clear()

    def wait(self, timeout: float | None = None) -> None:
        thread = self.thread
        if thread is not None:
            thread.join(timeout=timeout)

    def __enter__(self) -> "HumanPlayServerController":
        self.start()
        return self

    def __exit__(self, _exc_type: Any, _exc_value: Any, _traceback: Any) -> None:
        self.stop()


class HumanPlayRequestHandler(BaseHTTPRequestHandler):
    server_version = "GameAIHumanPlay/1.0"

    @property
    def session(self) -> GameSession:
        return self.server.session  # type: ignore[attr-defined]

    def log_message(self, _format: str, *_args: Any) -> None:
        return

    def _send_bytes(self, content: bytes, content_type: str, status: int = HTTPStatus.OK) -> None:
        self.send_response(int(status))
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(content)

    def _send_json(self, payload: dict[str, Any], status: int = HTTPStatus.OK) -> None:
        content = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self._send_bytes(content, "application/json; charset=utf-8", status)

    def _send_page(self, page: str) -> None:
        self._send_bytes(page.encode("utf-8"), "text/html; charset=utf-8")

    def _read_json(self) -> dict[str, Any]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise APIError(HTTPStatus.BAD_REQUEST, "Invalid Content-Length") from exc
        if length > 1024 * 1024:
            raise APIError(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "Request body is too large")
        raw = self.rfile.read(length)
        if not raw:
            return {}
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise APIError(HTTPStatus.BAD_REQUEST, "Request body must be valid JSON") from exc
        if not isinstance(payload, dict):
            raise APIError(HTTPStatus.BAD_REQUEST, "Request body must be a JSON object")
        return payload

    @staticmethod
    def _parse_action(payload: dict[str, Any]) -> int:
        raw_action = payload.get("action")
        if isinstance(raw_action, bool):
            raw_action = None
        if isinstance(raw_action, int):
            return raw_action
        if isinstance(raw_action, str) and raw_action.strip().lstrip("-").isdigit():
            return int(raw_action.strip())
        raise APIError(HTTPStatus.BAD_REQUEST, "Action must be an integer")

    def _state_result(self) -> dict[str, Any]:
        return {"ok": True, **self.session.state_payload()}

    def _handle_api_error(self, error: APIError) -> None:
        payload: dict[str, Any] = {"ok": False, "error": error.message}
        try:
            payload["state"] = self.session.state_payload()
        except Exception:
            pass
        self._send_json(payload, error.status)

    def do_GET(self) -> None:  # noqa: N802
        path = urlsplit(self.path).path
        try:
            if path == "/":
                self._send_page(MAIN_PAGE)
                return
            if path == "/tree":
                self._send_page(TREE_PAGE)
                return
            if path == "/favicon.ico":
                self._send_bytes(b"", "image/x-icon", HTTPStatus.NO_CONTENT)
                return
            if path == "/api/state":
                self._send_json(self._state_result())
                return
            tree_prefix = "/api/trees/"
            if path.startswith(tree_prefix):
                raw_id = path[len(tree_prefix) :]
                if not raw_id or "/" in raw_id:
                    raise APIError(HTTPStatus.NOT_FOUND, "Invalid search tree id")
                try:
                    tree_id = int(raw_id)
                except ValueError as exc:
                    raise APIError(HTTPStatus.NOT_FOUND, "Invalid search tree id") from exc
                self._send_json({"ok": True, **self.session.tree_payload(tree_id)})
                return
            raise APIError(HTTPStatus.NOT_FOUND, "Not found")
        except APIError as error:
            self._handle_api_error(error)
        except Exception as exc:
            self._handle_api_error(APIError(HTTPStatus.INTERNAL_SERVER_ERROR, str(exc)))

    def do_POST(self) -> None:  # noqa: N802
        path = urlsplit(self.path).path
        try:
            payload = self._read_json()
            if path == "/api/move":
                result = self.session.move_player(self._parse_action(payload))
            elif path == "/api/ai-turn":
                result = self.session.run_ai_turn()
            elif path == "/api/hint":
                result = self.session.get_hint()
            elif path == "/api/reset":
                result = self.session.reset()
            else:
                raise APIError(HTTPStatus.NOT_FOUND, "Not found")
            self._send_json({"ok": True, **result})
        except APIError as error:
            self._handle_api_error(error)
        except Exception as exc:
            self._handle_api_error(APIError(HTTPStatus.INTERNAL_SERVER_ERROR, str(exc)))


def read_server_state(pid_file: Path) -> dict[str, Any] | None:
    try:
        payload = json.loads(pid_file.read_text(encoding="utf-8"))
    except (FileNotFoundError, OSError, json.JSONDecodeError):
        return None
    return payload if isinstance(payload, dict) else None


def remove_server_state(pid_file: Path, expected_pid: int | None = None) -> None:
    state = read_server_state(pid_file)
    if expected_pid is not None and state is not None and state.get("pid") != expected_pid:
        return
    try:
        pid_file.unlink()
    except FileNotFoundError:
        pass


def process_is_running(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def write_server_state(pid_file: Path, controller: HumanPlayServerController) -> None:
    host, port = controller.address
    payload = {
        "pid": os.getpid(),
        "host": host,
        "port": port,
        "url": controller.url,
        "started_at": time.time(),
    }
    pid_file.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{pid_file.name}.",
        suffix=".tmp",
        dir=pid_file.parent,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, separators=(",", ":"))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_name, pid_file)
    finally:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass


def fetch_html(url: str) -> str:
    with urlopen(url, timeout=1.0) as response:
        if response.status != HTTPStatus.OK:
            raise RuntimeError(f"HTTP server returned status {response.status}")
        return response.read().decode("utf-8")


def wait_for_server_state(
    pid_file: Path,
    process: subprocess.Popen[bytes] | None = None,
    timeout: float = 10.0,
) -> tuple[dict[str, Any], str]:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"background server exited with status {process.returncode}")
        state = read_server_state(pid_file)
        if state is not None:
            try:
                pid = int(state["pid"])
                url = str(state["url"])
                if not process_is_running(pid):
                    raise RuntimeError("background server exited before becoming ready")
                html = fetch_html(url)
                return state, html
            except (KeyError, TypeError, ValueError, OSError, URLError, UnicodeDecodeError, RuntimeError) as exc:
                last_error = exc
        time.sleep(0.1)
    detail = f": {last_error}" if last_error is not None else ""
    raise RuntimeError(f"timed out waiting for HTTP server startup{detail}")


def terminate_spawned_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5.0)


def build_serve_argv(args: argparse.Namespace) -> list[str]:
    script_path = Path(__file__).resolve()
    command = [
        sys.executable,
        str(script_path),
        "_serve",
        "--config",
        str(args.config.resolve()),
        "--module-dir",
        str(args.module_dir.resolve()),
        "--seed",
        str(args.seed),
        "--host",
        str(args.host),
        "--port",
        str(args.port),
        "--pid-file",
        str(args.pid_file.resolve()),
    ]
    if args.ai_player is not None:
        command.extend(("--ai-player", str(args.ai_player)))
    if args.n_playout is not None:
        command.extend(("--n-playout", str(args.n_playout)))
    if args.c_puct is not None:
        command.extend(("--c-puct", str(args.c_puct)))
    if args.rollout_policy is not None:
        command.extend(("--rollout-policy", args.rollout_policy))
    if args.rollout_limit is not None:
        command.extend(("--rollout-limit", str(args.rollout_limit)))
    return command


def run_start(args: argparse.Namespace) -> int:
    pid_file = args.pid_file.expanduser().resolve()
    existing = read_server_state(pid_file)
    if existing is not None:
        try:
            existing_pid = int(existing["pid"])
        except (KeyError, TypeError, ValueError):
            existing_pid = -1
        if process_is_running(existing_pid):
            try:
                state, _html = wait_for_server_state(pid_file, timeout=5.0)
            except RuntimeError as exc:
                raise SystemExit(f"A server is already running but is not responding: {exc}") from exc
            url = str(state["url"])
            print(f"GameAI Human Play is already running at {url} (pid {state['pid']})", flush=True)
            if not args.no_browser:
                webbrowser.open(url, new=2)
            return 0
        remove_server_state(pid_file)

    child = subprocess.Popen(
        build_serve_argv(args),
        cwd=repo_root(),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    try:
        state, _html = wait_for_server_state(pid_file, process=child)
    except RuntimeError as exc:
        terminate_spawned_process(child)
        remove_server_state(pid_file, expected_pid=child.pid)
        raise SystemExit(f"Could not start background HTTP server: {exc}") from exc

    url = str(state["url"])
    print(f"GameAI Human Play started at {url} (pid {state['pid']})", flush=True)
    if not args.no_browser:
        webbrowser.open(url, new=2)
    return 0


def run_stop(args: argparse.Namespace) -> int:
    pid_file = args.pid_file.expanduser().resolve()
    state = read_server_state(pid_file)
    if state is None:
        print("GameAI Human Play is not running.", flush=True)
        return 0
    try:
        pid = int(state["pid"])
    except (KeyError, TypeError, ValueError) as exc:
        remove_server_state(pid_file)
        raise SystemExit(f"Invalid server state file: {pid_file}") from exc

    if not process_is_running(pid):
        remove_server_state(pid_file, expected_pid=pid)
        print("Removed stale GameAI Human Play state.", flush=True)
        return 0

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    except OSError as exc:
        raise SystemExit(f"Could not stop GameAI Human Play process {pid}: {exc}") from exc

    deadline = time.monotonic() + 5.0
    while process_is_running(pid) and time.monotonic() < deadline:
        time.sleep(0.1)
    if process_is_running(pid):
        raise SystemExit(f"Timed out waiting for GameAI Human Play process {pid} to stop")

    remove_server_state(pid_file, expected_pid=pid)
    print(f"GameAI Human Play stopped (pid {pid}).", flush=True)
    return 0


def run_server_process(args: argparse.Namespace) -> int:
    config = merge_runtime_config(args)
    module = load_native_module(args.module_dir)
    session = GameSession(
        module=module,
        ai_player=int(config["ai_player"]),
        n_playout=int(config["mcts"]["n_playout"]),
        c_puct=float(config["mcts"]["c_puct"]),
        rollout_policy=rollout_policy_id(config["mcts"].get("rollout_policy", "tactical")),
        rollout_limit=int(config["mcts"].get("rollout_limit", 32)),
        seed=args.seed,
    )
    controller = HumanPlayServerController(session, args.host, args.port)
    pid_file = args.pid_file.expanduser().resolve()
    stop_requested = threading.Event()

    def request_stop(_signum: int, _frame: Any) -> None:
        stop_requested.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    try:
        controller.start()
        write_server_state(pid_file, controller)
        while controller.running and not stop_requested.wait(timeout=0.2):
            pass
        if controller.thread_error is not None:
            raise controller.thread_error
        return 0
    finally:
        controller.stop()
        remove_server_state(pid_file, expected_pid=os.getpid())


def main() -> int:
    args = parse_args()
    if args.command == "start":
        return run_start(args)
    if args.command == "stop":
        return run_stop(args)
    if args.command == "_serve":
        return run_server_process(args)
    raise SystemExit(f"Unknown command: {args.command}")

WEB_DIR = Path(__file__).resolve().parent / "web"


def load_web_page(name: str) -> str:
    page_path = WEB_DIR / name
    try:
        return page_path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise RuntimeError(f"Web resource not found: {page_path}") from exc


MAIN_PAGE = load_web_page("humanplay.html")
TREE_PAGE = load_web_page("tree.html")


if __name__ == "__main__":
    raise SystemExit(main())
