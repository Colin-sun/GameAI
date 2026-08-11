#!/usr/bin/env python3
"""Small, reproducible AlphaZero-style tools for majority-rule UTT.

The native MCTS remains the production/default player.  This module provides
an optional NumPy policy/value network, neural PUCT search, self-play, and
teacher warm-start data so the experimental player can be evaluated under the
same game rules without adding a heavyweight runtime dependency.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np


BOARD_SIZE = 9
ACTION_SIZE = BOARD_SIZE * BOARD_SIZE
INPUT_PLANES = 10
INPUT_SIZE = INPUT_PLANES * ACTION_SIZE
RULE_VERSION = "majority-utt-v1"


def encode_state(game: Any) -> np.ndarray:
    """Encode a native game from the side-to-move perspective.

    The two stone planes are player-relative.  Meta-board status, the forced
    target board, player identity, and normalized move count are included so
    the network cannot confuse states with the same raw stones but different
    legal-action constraints.
    """

    board = np.asarray(game.get_board(), dtype=np.int8)
    meta_board = np.asarray(game.get_meta_board(), dtype=np.int8)
    current_player = int(game.get_current_player())
    opponent = 3 - current_player
    next_row, next_col = (int(value) for value in game.get_next_board())

    planes = [
        (board == current_player).astype(np.float32),
        (board == opponent).astype(np.float32),
    ]

    meta_planes = [
        (meta_board == current_player).astype(np.float32),
        (meta_board == opponent).astype(np.float32),
        (meta_board == 3).astype(np.float32),
        (meta_board == 0).astype(np.float32),
    ]
    for meta_plane in meta_planes:
        planes.append(np.repeat(np.repeat(meta_plane, 3, axis=0), 3, axis=1))

    target_plane = np.zeros((BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
    free_plane = np.zeros((BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
    if next_row == -1 and next_col == -1:
        free_plane.fill(1.0)
    elif 0 <= next_row < 3 and 0 <= next_col < 3:
        target_plane[next_row * 3 : next_row * 3 + 3, next_col * 3 : next_col * 3 + 3] = 1.0
    else:
        raise ValueError(f"invalid next board from native game: {(next_row, next_col)}")
    planes.extend((target_plane, free_plane))

    player_plane = np.full(
        (BOARD_SIZE, BOARD_SIZE), float(current_player == 1), dtype=np.float32
    )
    step_plane = np.full(
        (BOARD_SIZE, BOARD_SIZE), float(game.get_step()) / ACTION_SIZE, dtype=np.float32
    )
    planes.extend((player_plane, step_plane))
    return np.stack(planes, axis=0).reshape(INPUT_SIZE)


def legal_actions(game: Any) -> list[int]:
    return [int(action) for action in game.get_valid_actions()]


def masked_policy(policy: np.ndarray, actions: Iterable[int]) -> np.ndarray:
    """Mask a 81-action policy and normalize it over legal actions."""

    action_list = [int(action) for action in actions]
    result = np.zeros(ACTION_SIZE, dtype=np.float32)
    if not action_list:
        return result
    values = np.asarray(policy, dtype=np.float64)[action_list]
    values = np.maximum(values, 0.0)
    total = float(values.sum())
    if not math.isfinite(total) or total <= 0.0:
        values = np.full(len(action_list), 1.0 / len(action_list), dtype=np.float64)
    else:
        values /= total
    result[action_list] = values.astype(np.float32)
    return result


def _local_board_state(board: np.ndarray) -> int:
    for index in range(3):
        if board[index, 0] != 0 and board[index, 0] == board[index, 1] == board[index, 2]:
            return int(board[index, 0])
        if board[0, index] != 0 and board[0, index] == board[1, index] == board[2, index]:
            return int(board[0, index])
    if board[0, 0] != 0 and board[0, 0] == board[1, 1] == board[2, 2]:
        return int(board[0, 0])
    if board[0, 2] != 0 and board[0, 2] == board[1, 1] == board[2, 0]:
        return int(board[0, 2])
    return 0 if np.any(board == 0) else 3


def _majority_winner(meta_board: np.ndarray) -> int:
    own = int(np.count_nonzero(meta_board == 1))
    opponent = int(np.count_nonzero(meta_board == 2))
    draws = int(np.count_nonzero(meta_board == 3))
    resolved = int(np.count_nonzero(meta_board != 0))
    threshold = (9 - draws) // 2 + 1
    if own >= threshold:
        return 1
    if opponent >= threshold:
        return 2
    if resolved == 9:
        if own > opponent:
            return 1
        if opponent > own:
            return 2
        return -1
    return 0


def tactical_policy(game: Any, actions: Iterable[int]) -> np.ndarray:
    """Return a uniform policy on the highest native tactical priority.

    This mirrors ``UltimateTicTacToe::select_rollout_action`` and the
    majority-rule terminal test. It is an optional prior for the neural search
    and is deliberately separate from the learned policy.
    """

    action_list = [int(action) for action in actions]
    result = np.zeros(ACTION_SIZE, dtype=np.float32)
    if not action_list:
        return result
    board = np.asarray(game.get_board(), dtype=np.int8)
    meta_board = np.asarray(game.get_meta_board(), dtype=np.int8)
    player = int(game.get_current_player())
    opponent = 3 - player
    priorities: dict[int, int] = {}
    for action in action_list:
        row, col = divmod(action, BOARD_SIZE)
        local_row, local_col = row // 3, col // 3
        local = board[local_row * 3 : local_row * 3 + 3, local_col * 3 : local_col * 3 + 3]
        next_local = local.copy()
        next_local[row % 3, col % 3] = player
        completes_local = _local_board_state(next_local) == player
        wins_meta = False
        if completes_local:
            next_meta = meta_board.copy()
            next_meta[local_row, local_col] = player
            wins_meta = _majority_winner(next_meta) == player

        opponent_local = local.copy()
        opponent_local[row % 3, col % 3] = opponent
        opponent_completes = _local_board_state(opponent_local) == opponent
        opponent_wins_meta = False
        if opponent_completes:
            next_meta = meta_board.copy()
            next_meta[local_row, local_col] = opponent
            opponent_wins_meta = _majority_winner(next_meta) == opponent

        priorities[action] = (
            4 if wins_meta else 3 if opponent_wins_meta else 2 if completes_local else 1 if opponent_completes else 0
        )
    best_priority = max(priorities.values())
    best_actions = [action for action in action_list if priorities[action] == best_priority]
    result[best_actions] = 1.0 / len(best_actions)
    return result


def heuristic_value(game: Any, player: int) -> float:
    """Match the native majority-rule cutoff heuristic."""

    done, winner = game.get_done_winner()
    if done:
        if int(winner) == -1:
            return 0.0
        return 1.0 if int(winner) == int(player) else -1.0
    if player not in (1, 2):
        return 0.0

    meta_board = np.asarray(game.get_meta_board(), dtype=np.int8)
    opponent = 3 - int(player)
    score = 0.35 * float(
        np.count_nonzero(meta_board == player) - np.count_nonzero(meta_board == opponent)
    )
    local_lines = (
        ((0, 0), (0, 1), (0, 2)),
        ((1, 0), (1, 1), (1, 2)),
        ((2, 0), (2, 1), (2, 2)),
        ((0, 0), (1, 0), (2, 0)),
        ((0, 1), (1, 1), (2, 1)),
        ((0, 2), (1, 2), (2, 2)),
        ((0, 0), (1, 1), (2, 2)),
        ((0, 2), (1, 1), (2, 0)),
    )
    board = np.asarray(game.get_board(), dtype=np.int8)
    for meta_row in range(3):
        for meta_col in range(3):
            if meta_board[meta_row, meta_col] != 0:
                continue
            sub_board = board[meta_row * 3 : meta_row * 3 + 3, meta_col * 3 : meta_col * 3 + 3]
            for line in local_lines:
                values = [int(sub_board[row, col]) for row, col in line]
                own = values.count(int(player))
                theirs = values.count(opponent)
                if theirs == 0 and own > 0:
                    score += 0.055 if own == 2 else 0.012
                elif own == 0 and theirs > 0:
                    score -= 0.055 if theirs == 2 else 0.012
    return float(np.tanh(score))


class PolicyValueNetwork:
    """A deterministic NumPy MLP with policy and value heads."""

    format_version = 1

    def __init__(
        self,
        hidden_size: int = 128,
        learning_rate: float = 0.003,
        weight_decay: float = 1.0e-4,
        seed: int = 0,
    ) -> None:
        if hidden_size <= 0:
            raise ValueError("hidden_size must be positive")
        if learning_rate <= 0.0:
            raise ValueError("learning_rate must be positive")
        if weight_decay < 0.0:
            raise ValueError("weight_decay must be non-negative")
        self.hidden_size = int(hidden_size)
        self.learning_rate = float(learning_rate)
        self.weight_decay = float(weight_decay)
        rng = np.random.default_rng(seed)
        input_scale = math.sqrt(2.0 / INPUT_SIZE)
        hidden_scale = math.sqrt(2.0 / self.hidden_size)
        self.w1 = (rng.standard_normal((INPUT_SIZE, self.hidden_size)) * input_scale).astype(
            np.float32
        )
        self.b1 = np.zeros(self.hidden_size, dtype=np.float32)
        self.wp = (rng.standard_normal((self.hidden_size, ACTION_SIZE)) * hidden_scale).astype(
            np.float32
        )
        self.bp = np.zeros(ACTION_SIZE, dtype=np.float32)
        self.wv = (rng.standard_normal((self.hidden_size, 1)) * hidden_scale).astype(np.float32)
        self.bv = np.zeros(1, dtype=np.float32)
        self._adam_m = {name: np.zeros_like(value) for name, value in self._parameters()}
        self._adam_v = {name: np.zeros_like(value) for name, value in self._parameters()}
        self._adam_step = 0

    def _parameters(self) -> list[tuple[str, np.ndarray]]:
        return [
            ("w1", self.w1),
            ("b1", self.b1),
            ("wp", self.wp),
            ("bp", self.bp),
            ("wv", self.wv),
            ("bv", self.bv),
        ]

    @staticmethod
    def _softmax(logits: np.ndarray) -> np.ndarray:
        shifted = logits - np.max(logits, axis=1, keepdims=True)
        values = np.exp(shifted)
        return values / np.sum(values, axis=1, keepdims=True)

    def _forward(self, states: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        x = np.asarray(states, dtype=np.float32)
        if x.ndim != 2 or x.shape[1] != INPUT_SIZE:
            raise ValueError(f"states must have shape (batch, {INPUT_SIZE})")
        z1 = x @ self.w1 + self.b1
        hidden = np.maximum(z1, 0.0)
        logits = hidden @ self.wp + self.bp
        policy = self._softmax(logits)
        value = np.tanh((hidden @ self.wv + self.bv).ravel())
        return z1, policy, value

    def predict(self, state: np.ndarray) -> tuple[np.ndarray, float]:
        values = np.asarray(state, dtype=np.float32)
        single = values.ndim == 1
        if single:
            values = values[None, :]
        _, policy, value = self._forward(values)
        if single:
            return policy[0].copy(), float(value[0])
        return policy, value

    def train_batch(
        self,
        states: np.ndarray,
        policy_targets: np.ndarray,
        value_targets: np.ndarray,
        policy_weight: float = 1.0,
        value_weight: float = 1.0,
    ) -> dict[str, float]:
        x = np.asarray(states, dtype=np.float32)
        target_policy = np.asarray(policy_targets, dtype=np.float32)
        target_value = np.asarray(value_targets, dtype=np.float32).reshape(-1)
        if x.ndim != 2 or x.shape[1] != INPUT_SIZE:
            raise ValueError(f"states must have shape (batch, {INPUT_SIZE})")
        if target_policy.shape != (len(x), ACTION_SIZE):
            raise ValueError("policy target shape does not match the batch")
        if target_value.shape != (len(x),):
            raise ValueError("value target shape does not match the batch")
        if len(x) == 0:
            raise ValueError("cannot train on an empty batch")

        z1, policy, value = self._forward(x)
        clipped_policy = np.clip(policy, 1.0e-8, 1.0)
        policy_loss = -np.sum(target_policy * np.log(clipped_policy), axis=1).mean()
        value_loss = np.mean((value - target_value) ** 2)
        batch_size = float(len(x))

        d_logits = (policy - target_policy) * (float(policy_weight) / batch_size)
        d_value = (
            2.0
            * (value - target_value)
            * (1.0 - value * value)
            * (float(value_weight) / batch_size)
        )
        hidden = np.maximum(z1, 0.0)
        gradients = {
            "wp": hidden.T @ d_logits + self.weight_decay * self.wp,
            "bp": np.sum(d_logits, axis=0),
            "wv": hidden.T @ d_value[:, None] + self.weight_decay * self.wv,
            "bv": np.sum(d_value, axis=0),
        }
        d_hidden = d_logits @ self.wp.T + d_value[:, None] @ self.wv.T
        d_z1 = d_hidden * (z1 > 0.0)
        gradients["w1"] = x.T @ d_z1 + self.weight_decay * self.w1
        gradients["b1"] = np.sum(d_z1, axis=0)

        self._adam_step += 1
        beta1 = 0.9
        beta2 = 0.999
        epsilon = 1.0e-8
        correction1 = 1.0 - beta1**self._adam_step
        correction2 = 1.0 - beta2**self._adam_step
        for name, parameter in self._parameters():
            self._adam_m[name] = beta1 * self._adam_m[name] + (1.0 - beta1) * gradients[name]
            self._adam_v[name] = beta2 * self._adam_v[name] + (1.0 - beta2) * gradients[name] ** 2
            mean = self._adam_m[name] / correction1
            variance = self._adam_v[name] / correction2
            parameter -= self.learning_rate * mean / (np.sqrt(variance) + epsilon)

        return {
            "policy_loss": float(policy_loss),
            "value_loss": float(value_loss),
            "loss": float(policy_loss + value_loss),
        }

    def save(self, path: Path) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        metadata = {
            "format_version": self.format_version,
            "rule_version": RULE_VERSION,
            "input_planes": INPUT_PLANES,
            "input_size": INPUT_SIZE,
            "action_size": ACTION_SIZE,
            "hidden_size": self.hidden_size,
            "learning_rate": self.learning_rate,
            "weight_decay": self.weight_decay,
        }
        np.savez_compressed(
            path,
            w1=self.w1,
            b1=self.b1,
            wp=self.wp,
            bp=self.bp,
            wv=self.wv,
            bv=self.bv,
            metadata=np.asarray(json.dumps(metadata, sort_keys=True)),
        )

    @classmethod
    def load(cls, path: Path) -> "PolicyValueNetwork":
        with np.load(Path(path), allow_pickle=False) as data:
            metadata = json.loads(str(data["metadata"].item()))
            if metadata.get("rule_version") != RULE_VERSION:
                raise ValueError(
                    f"checkpoint rule {metadata.get('rule_version')!r} != {RULE_VERSION!r}"
                )
            model = cls(
                hidden_size=int(metadata["hidden_size"]),
                learning_rate=float(metadata.get("learning_rate", 0.003)),
                weight_decay=float(metadata.get("weight_decay", 1.0e-4)),
                seed=0,
            )
            for name in ("w1", "b1", "wp", "bp", "wv", "bv"):
                loaded = np.asarray(data[name], dtype=np.float32)
                parameter = getattr(model, name)
                if loaded.shape != parameter.shape:
                    raise ValueError(f"checkpoint parameter {name} has invalid shape {loaded.shape}")
                parameter[...] = loaded
            return model


@dataclass
class NeuralSearchConfig:
    simulations: int = 128
    c_puct: float = 1.25
    dirichlet_alpha: float = 0.30
    dirichlet_epsilon: float = 0.25
    tactical_prior_weight: float = 0.0
    rollout_value_weight: float = 0.0
    rollout_limit: int = 32
    force_tactical: bool = False
    inference_batch_size: int = 1
    policy_exponent: float = 1.0
    root_selection: str = "visits"

    def __post_init__(self) -> None:
        if self.simulations <= 0:
            raise ValueError("simulations must be positive")
        if self.c_puct <= 0.0:
            raise ValueError("c_puct must be positive")
        if self.dirichlet_alpha <= 0.0:
            raise ValueError("dirichlet_alpha must be positive")
        if not 0.0 <= self.dirichlet_epsilon <= 1.0:
            raise ValueError("dirichlet_epsilon must be in [0, 1]")
        if not 0.0 <= self.tactical_prior_weight <= 1.0:
            raise ValueError("tactical_prior_weight must be in [0, 1]")
        if not 0.0 <= self.rollout_value_weight <= 1.0:
            raise ValueError("rollout_value_weight must be in [0, 1]")
        if self.rollout_limit <= 0:
            raise ValueError("rollout_limit must be positive")
        if self.inference_batch_size <= 0:
            raise ValueError("inference_batch_size must be positive")
        if self.policy_exponent <= 0.0:
            raise ValueError("policy_exponent must be positive")
        if self.root_selection not in {"visits", "q"}:
            raise ValueError("root_selection must be 'visits' or 'q'")


class SearchNode:
    __slots__ = ("prior", "visit_count", "value_sum", "children")

    def __init__(self, prior: float = 1.0) -> None:
        self.prior = float(prior)
        self.visit_count = 0
        self.value_sum = 0.0
        self.children: dict[int, SearchNode] = {}

    @property
    def q(self) -> float:
        return self.value_sum / self.visit_count if self.visit_count else 0.0


class NeuralMCTS:
    def __init__(
        self,
        model: PolicyValueNetwork,
        config: NeuralSearchConfig,
        seed: int = 0,
    ) -> None:
        self.model = model
        self.config = config
        self.rng = np.random.default_rng(seed)
        self.root = SearchNode()
        self._root_tactical_actions: set[int] | None = None

    def _expand_with_prediction(
        self,
        node: SearchNode,
        game: Any,
        policy: np.ndarray,
        value: float,
    ) -> float:
        done, winner = game.get_done_winner()
        if done:
            current_player = int(game.get_current_player())
            if int(winner) == -1:
                return 0.0
            return 1.0 if int(winner) == current_player else -1.0

        actions = legal_actions(game)
        if not actions:
            raise RuntimeError("non-terminal native game has no legal actions")
        adjusted_policy = np.power(
            np.maximum(np.asarray(policy, dtype=np.float32), 0.0),
            self.config.policy_exponent,
        )
        priors = masked_policy(adjusted_policy, actions)
        if self.config.tactical_prior_weight > 0.0:
            heuristic = tactical_policy(game, actions)
            weight = self.config.tactical_prior_weight
            priors = masked_policy((1.0 - weight) * priors + weight * heuristic, actions)
        for action in actions:
            node.children[action] = SearchNode(float(priors[action]))
        if self.config.rollout_value_weight == 0.0:
            return float(value)
        rollout_value = self._tactical_rollout_value(game.clone())
        weight = self.config.rollout_value_weight
        return float((1.0 - weight) * value + weight * rollout_value)

    def _expand(self, node: SearchNode, game: Any) -> float:
        policy, value = self.model.predict(encode_state(game))
        return self._expand_with_prediction(node, game, policy, float(value))

    def _tactical_rollout_value(self, game: Any) -> float:
        perspective_player = int(game.get_current_player())
        for _ in range(self.config.rollout_limit):
            done, winner = game.get_done_winner()
            if done:
                return outcome_for_player(int(winner), perspective_player)
            actions = legal_actions(game)
            if not actions:
                raise RuntimeError("non-terminal game has no tactical rollout action")
            policy = tactical_policy(game, actions)
            candidates = np.flatnonzero(policy > 0.0)
            action = int(self.rng.choice(candidates, p=policy[candidates]))
            if not game.make_move(action):
                raise RuntimeError(f"tactical rollout selected invalid action {action}")
        return heuristic_value(game, perspective_player)

    def _add_root_noise(self, node: SearchNode) -> None:
        actions = sorted(node.children)
        if len(actions) < 2 or self.config.dirichlet_epsilon == 0.0:
            return
        noise = self.rng.dirichlet(
            np.full(len(actions), self.config.dirichlet_alpha, dtype=np.float64)
        )
        epsilon = self.config.dirichlet_epsilon
        for action, value in zip(actions, noise):
            child = node.children[action]
            child.prior = (1.0 - epsilon) * child.prior + epsilon * float(value)

    def _select_child(self, node: SearchNode) -> tuple[int, SearchNode]:
        parent_visits = math.sqrt(float(node.visit_count + 1))
        best_action = -1
        best_child: SearchNode | None = None
        best_score = -float("inf")
        for action in sorted(node.children):
            child = node.children[action]
            score = child.q + self.config.c_puct * child.prior * parent_visits / (
                1.0 + child.visit_count
            )
            if score > best_score or (score == best_score and action < best_action):
                best_score = score
                best_action = action
                best_child = child
        if best_child is None:
            raise RuntimeError("cannot select a child from an empty neural MCTS node")
        return best_action, best_child

    @staticmethod
    def _backup(path: list[SearchNode], value: float) -> None:
        for node in reversed(path):
            node.visit_count += 1
            node.value_sum += value
            value = -value

    @staticmethod
    def _backup_reserved(path: list[SearchNode], value: float) -> None:
        """Finish a simulation whose visit counts were reserved in a batch."""

        for node in reversed(path):
            node.value_sum += value
            value = -value

    def _search_sequential(self, game: Any) -> None:
        for _ in range(self.config.simulations):
            state = game.clone()
            node = self.root
            path = [node]
            while node.children:
                action, node = self._select_child(node)
                if not state.make_move(action):
                    raise RuntimeError(f"neural MCTS selected invalid action {action}")
                path.append(node)
                done, winner = state.get_done_winner()
                if done:
                    current_player = int(state.get_current_player())
                    if int(winner) == -1:
                        leaf_value = 0.0
                    else:
                        leaf_value = 1.0 if int(winner) == current_player else -1.0
                    break
            else:
                leaf_value = self._expand(node, state)
            # The child Q is stored from its parent's point of view.
            self._backup(path, -float(leaf_value))

    def _search_batched(self, game: Any) -> None:
        remaining = self.config.simulations
        batch_size = self.config.inference_batch_size
        while remaining > 0:
            current_batch = min(batch_size, remaining)
            leaves: list[tuple[SearchNode, Any, list[SearchNode]]] = []
            for _ in range(current_batch):
                state = game.clone()
                node = self.root
                path = [node]
                terminal_value: float | None = None
                while node.children:
                    action, node = self._select_child(node)
                    if not state.make_move(action):
                        raise RuntimeError(f"neural MCTS selected invalid action {action}")
                    path.append(node)
                    done, winner = state.get_done_winner()
                    if done:
                        current_player = int(state.get_current_player())
                        if int(winner) == -1:
                            terminal_value = 0.0
                        else:
                            terminal_value = (
                                1.0 if int(winner) == current_player else -1.0
                            )
                        break

                # Reserve visits while selecting the rest of the batch. This
                # is a lightweight virtual-loss scheme that prevents every
                # batch member from following the same root edge.
                for reserved_node in path:
                    reserved_node.visit_count += 1
                if terminal_value is None:
                    leaves.append((node, state, path))
                else:
                    self._backup_reserved(path, -float(terminal_value))

            if leaves:
                encoded = np.stack([encode_state(state) for _, state, _ in leaves])
                policies, values = self.model.predict(encoded)
                expanded_nodes: set[int] = set()
                for index, (node, state, path) in enumerate(leaves):
                    if id(node) not in expanded_nodes:
                        leaf_value = self._expand_with_prediction(
                            node, state, policies[index], float(values[index])
                        )
                        expanded_nodes.add(id(node))
                    else:
                        leaf_value = float(values[index])
                    self._backup_reserved(path, -float(leaf_value))
            remaining -= current_batch

    def search(self, game: Any, add_root_noise: bool = False) -> SearchNode:
        self._root_tactical_actions = None
        done, _ = game.get_done_winner()
        if done:
            return self.root
        if not self.root.children:
            self._expand(self.root, game)
        if self.config.force_tactical:
            actions = legal_actions(game)
            tactical = tactical_policy(game, actions)
            tactical_actions = np.flatnonzero(tactical > 0.0)
            # ``tactical_policy`` is uniform over all legal actions when no
            # immediate local/meta tactic exists.  A strict support subset is
            # therefore a reliable hard safety set for the root move.
            if 0 < len(tactical_actions) < len(actions):
                self._root_tactical_actions = {int(action) for action in tactical_actions}
        if add_root_noise:
            self._add_root_noise(self.root)

        if self.config.inference_batch_size == 1:
            self._search_sequential(game)
        else:
            self._search_batched(game)
        return self.root

    def visit_policy(self, temperature: float = 1.0) -> np.ndarray:
        policy = np.zeros(ACTION_SIZE, dtype=np.float32)
        actions = sorted(self.root.children)
        if not actions:
            return policy
        visits = np.asarray(
            [float(self.root.children[action].visit_count) for action in actions],
            dtype=np.float64,
        )
        if temperature <= 1.0e-6:
            chosen = actions[int(np.argmax(visits))]
            policy[chosen] = 1.0
            return policy
        scaled = np.power(np.maximum(visits, 1.0e-12), 1.0 / float(temperature))
        total = float(scaled.sum())
        if total <= 0.0 or not math.isfinite(total):
            scaled.fill(1.0 / len(actions))
        else:
            scaled /= total
        policy[actions] = scaled.astype(np.float32)
        return policy

    def choose_action(self, temperature: float = 0.0) -> int:
        policy = self.visit_policy(temperature)
        actions = np.flatnonzero(policy > 0.0)
        if len(actions) == 0:
            raise RuntimeError("neural MCTS has no action to choose")
        if self._root_tactical_actions:
            restricted = np.asarray(
                [action for action in actions if int(action) in self._root_tactical_actions],
                dtype=np.int64,
            )
            if len(restricted):
                actions = restricted
        if temperature <= 1.0e-6:
            return int(actions[np.argmax(policy[actions])])
        return int(self.rng.choice(actions, p=policy[actions] / policy[actions].sum()))

    def advance(self, action: int) -> None:
        action = int(action)
        child = self.root.children.get(action)
        if child is None:
            self.root = SearchNode()
        else:
            self.root = child


@dataclass
class TrainingExample:
    state: np.ndarray
    policy: np.ndarray
    player: int
    value: float = 0.0


def outcome_for_player(winner: int, player: int) -> float:
    if int(winner) == -1:
        return 0.0
    return 1.0 if int(winner) == int(player) else -1.0


def _snapshot_policy(snapshot: dict[str, Any], actions: list[int], selected_action: int) -> np.ndarray:
    result = np.zeros(ACTION_SIZE, dtype=np.float32)
    visits_by_action = {
        int(node["action"]): max(0, int(node["visits"]))
        for node in snapshot.get("nodes", [])
        if int(node.get("parent_id", -1)) == 0
    }
    visits = np.asarray([visits_by_action.get(action, 0) for action in actions], dtype=np.float64)
    if visits.sum() <= 0.0:
        if selected_action not in actions:
            raise RuntimeError("teacher selected an action outside the legal action set")
        result[selected_action] = 1.0
    else:
        result[actions] = (visits / visits.sum()).astype(np.float32)
    return result


def play_teacher_game(
    module: Any,
    n_playout: int,
    c_puct: float,
    seed: int,
    tree_value_weight: float = 0.0,
) -> tuple[list[TrainingExample], int, int]:
    """Generate policy targets from the existing tactical native MCTS."""

    if not 0.0 <= tree_value_weight <= 1.0:
        raise ValueError("tree_value_weight must be in [0, 1]")

    player_one = module.MCTSPure(
        n_playout=n_playout,
        c_puct=c_puct,
        seed=int(seed) & 0xFFFFFFFF,
        capture_search_tree=True,
        rollout_policy=1,
        rollout_limit=32,
    )
    player_two = module.MCTSPure(
        n_playout=n_playout,
        c_puct=c_puct,
        seed=(int(seed) + 1) & 0xFFFFFFFF,
        capture_search_tree=True,
        rollout_policy=1,
        rollout_limit=32,
    )
    game = module.UltimateTicTacToe()
    records: list[TrainingExample] = []
    moves = 0
    while True:
        done, winner = game.get_done_winner()
        if done:
            return [
                TrainingExample(
                    item.state,
                    item.policy,
                    item.player,
                    (
                        float(tree_value_weight) * float(item.value)
                        + (1.0 - float(tree_value_weight))
                        * outcome_for_player(winner, item.player)
                    ),
                )
                for item in records
            ], int(winner), moves
        if moves >= ACTION_SIZE:
            raise RuntimeError("teacher game exceeded the maximum move count")
        player = int(game.get_current_player())
        actions = legal_actions(game)
        engine = player_one if player == 1 else player_two
        state = encode_state(game)
        action = int(engine.get_move(game))
        snapshot = engine.get_last_search_tree()
        policy = _snapshot_policy(snapshot, actions, action)
        root_value = 0.0
        if tree_value_weight > 0.0:
            root_nodes = snapshot.get("nodes", [])
            if root_nodes:
                root_value = float(root_nodes[0].get("q", 0.0))
        records.append(TrainingExample(state, policy, player, root_value))
        if player == 1:
            player_two.update_with_move(action)
        else:
            player_one.update_with_move(action)
        if not game.make_move(action):
            raise RuntimeError(f"teacher MCTS produced invalid action {action}")
        moves += 1


def play_self_play_game(
    module: Any,
    model: PolicyValueNetwork,
    config: NeuralSearchConfig,
    seed: int,
    temperature_moves: int = 12,
) -> tuple[list[TrainingExample], int, int]:
    """Generate one AlphaZero-style game with visit-policy targets."""

    player_one = NeuralMCTS(model, config, seed=int(seed) + 11)
    player_two = NeuralMCTS(model, config, seed=int(seed) + 23)
    game = module.UltimateTicTacToe()
    records: list[TrainingExample] = []
    moves = 0
    while True:
        done, winner = game.get_done_winner()
        if done:
            return [
                TrainingExample(
                    item.state,
                    item.policy,
                    item.player,
                    outcome_for_player(winner, item.player),
                )
                for item in records
            ], int(winner), moves
        if moves >= ACTION_SIZE:
            raise RuntimeError("self-play game exceeded the maximum move count")
        player = int(game.get_current_player())
        search = player_one if player == 1 else player_two
        state = encode_state(game)
        search.search(game, add_root_noise=True)
        policy = search.visit_policy(temperature=1.0)
        temperature = 1.0 if moves < temperature_moves else 0.0
        action = search.choose_action(temperature=temperature)
        records.append(TrainingExample(state, policy, player))
        player_one.advance(action)
        player_two.advance(action)
        if not game.make_move(action):
            raise RuntimeError(f"neural MCTS produced invalid action {action}")
        moves += 1


def _extend_replay(replay: list[TrainingExample], examples: list[TrainingExample], capacity: int) -> None:
    for example in examples:
        state_planes = example.state.reshape(INPUT_PLANES, BOARD_SIZE, BOARD_SIZE)
        policy_grid = example.policy.reshape(BOARD_SIZE, BOARD_SIZE)
        for flip in (False, True):
            for rotation in range(4):
                transformed_state = np.rot90(state_planes, rotation, axes=(1, 2))
                transformed_policy = np.rot90(policy_grid, rotation)
                if flip:
                    transformed_state = np.flip(transformed_state, axis=2)
                    transformed_policy = np.flip(transformed_policy, axis=1)
                replay.append(
                    TrainingExample(
                        np.ascontiguousarray(transformed_state.reshape(INPUT_SIZE)),
                        np.ascontiguousarray(transformed_policy.reshape(ACTION_SIZE)),
                        example.player,
                        example.value,
                    )
                )
    if len(replay) > capacity:
        del replay[: len(replay) - capacity]


def train_model(
    module: Any,
    model: PolicyValueNetwork,
    seed: int,
    teacher_games: int = 4,
    teacher_playout: int = 96,
    teacher_c_puct: float = 0.3,
    iterations: int = 2,
    self_play_games: int = 8,
    self_play_config: NeuralSearchConfig | None = None,
    temperature_moves: int = 12,
    epochs: int = 4,
    batch_size: int = 64,
    replay_capacity: int = 20000,
    symmetry_augmentation: bool = True,
) -> dict[str, Any]:
    """Train a model and return JSON-serializable run statistics."""

    if min(teacher_games, iterations, self_play_games, epochs, batch_size) < 0:
        raise ValueError("training counts cannot be negative")
    if teacher_playout <= 0 or replay_capacity <= 0:
        raise ValueError("teacher_playout and replay_capacity must be positive")
    config = self_play_config or NeuralSearchConfig()
    replay: list[TrainingExample] = []
    rng = np.random.default_rng(seed)
    teacher_wdl = {"wins": 0, "draws": 0, "losses": 0}
    self_play_wdl = {"wins": 0, "draws": 0, "losses": 0}
    positions = 0

    def record_wdl(bucket: dict[str, int], winner: int) -> None:
        if winner == -1:
            bucket["draws"] += 1
        elif winner == 1:
            bucket["wins"] += 1
        else:
            bucket["losses"] += 1

    for game_index in range(teacher_games):
        examples, winner, _ = play_teacher_game(
            module,
            n_playout=teacher_playout,
            c_puct=teacher_c_puct,
            seed=int(seed + 1000 + game_index * 17),
        )
        if symmetry_augmentation:
            _extend_replay(replay, examples, replay_capacity)
        else:
            replay.extend(examples)
            if len(replay) > replay_capacity:
                del replay[: len(replay) - replay_capacity]
        positions += len(examples)
        record_wdl(teacher_wdl, winner)

    iteration_metrics: list[dict[str, Any]] = []
    for iteration in range(iterations):
        iteration_games = 0
        iteration_positions = 0
        for game_index in range(self_play_games):
            examples, winner, _ = play_self_play_game(
                module,
                model,
                config,
                seed=int(seed + 100000 + iteration * 1000 + game_index * 31),
                temperature_moves=temperature_moves,
            )
            if symmetry_augmentation:
                _extend_replay(replay, examples, replay_capacity)
            else:
                replay.extend(examples)
                if len(replay) > replay_capacity:
                    del replay[: len(replay) - replay_capacity]
            positions += len(examples)
            iteration_positions += len(examples)
            iteration_games += 1
            record_wdl(self_play_wdl, winner)

        losses: list[dict[str, float]] = []
        if replay and epochs > 0:
            for _ in range(epochs):
                order = rng.permutation(len(replay))
                for start in range(0, len(order), batch_size):
                    selected = order[start : start + batch_size]
                    states = np.stack([replay[int(index)].state for index in selected])
                    policies = np.stack([replay[int(index)].policy for index in selected])
                    targets = np.asarray(
                        [float(replay[int(index)].value) for index in selected], dtype=np.float32
                    )
                    losses.append(model.train_batch(states, policies, targets))
        mean_loss = {
            name: float(np.mean([item[name] for item in losses])) if losses else 0.0
            for name in ("loss", "policy_loss", "value_loss")
        }
        iteration_metrics.append(
            {
                "iteration": iteration + 1,
                "games": iteration_games,
                "positions": iteration_positions,
                "replay_size": len(replay),
                **mean_loss,
            }
        )

    return {
        "rule_version": RULE_VERSION,
        "teacher_games": teacher_games,
        "teacher_playout": teacher_playout,
        "teacher_wdl": teacher_wdl,
        "self_play_games": iterations * self_play_games,
        "self_play_simulations": config.simulations,
        "self_play_c_puct": config.c_puct,
        "symmetry_augmentation": symmetry_augmentation,
        "self_play_wdl": self_play_wdl,
        "positions": positions,
        "replay_size": len(replay),
        "iterations": iteration_metrics,
    }


def _record_match_result(records: list[dict[str, Any]], candidate_first: bool, winner: int, moves: int) -> None:
    if winner == -1:
        candidate_winner = 0
    elif (winner == 1) == candidate_first:
        candidate_winner = 1
    else:
        candidate_winner = 2
    records.append(
        {
            "candidate_first": candidate_first,
            "candidate_winner": candidate_winner,
            "winner": int(winner),
            "moves": int(moves),
        }
    )


def _play_match_game(
    module: Any,
    model: PolicyValueNetwork,
    neural_config: NeuralSearchConfig,
    baseline_n_playout: int,
    baseline_c_puct: float,
    candidate_first: bool,
    seed: int,
) -> tuple[int, int]:
    candidate_player = 1 if candidate_first else 2
    neural = NeuralMCTS(model, neural_config, seed=int(seed) + 7)
    baseline = module.MCTSPure(
        n_playout=baseline_n_playout,
        c_puct=baseline_c_puct,
        seed=(int(seed) + 13) & 0xFFFFFFFF,
        rollout_policy=1,
        rollout_limit=32,
    )
    game = module.UltimateTicTacToe()
    moves = 0
    while True:
        done, winner = game.get_done_winner()
        if done:
            return int(winner), moves
        if moves >= ACTION_SIZE:
            raise RuntimeError("evaluation game exceeded the maximum move count")
        current_player = int(game.get_current_player())
        if current_player == candidate_player:
            neural.search(game, add_root_noise=False)
            action = neural.choose_action(temperature=0.0)
            neural.advance(action)
            baseline.update_with_move(action)
        else:
            action = int(baseline.get_move(game))
            neural.advance(action)
        if not game.make_move(action):
            raise RuntimeError(f"evaluation player produced invalid action {action}")
        moves += 1


def evaluate_model_against_mcts(
    module: Any,
    model: PolicyValueNetwork,
    games_per_side: int,
    seeds: Iterable[int],
    neural_config: NeuralSearchConfig,
    baseline_n_playout: int = 3000,
    baseline_c_puct: float = 0.3,
) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    seed_list = [int(seed) for seed in seeds]
    for seed in seed_list:
        for offset in range(games_per_side):
            winner, moves = _play_match_game(
                module,
                model,
                neural_config,
                baseline_n_playout,
                baseline_c_puct,
                candidate_first=True,
                seed=seed + offset * 101,
            )
            _record_match_result(records, True, winner, moves)
        for offset in range(games_per_side):
            winner, moves = _play_match_game(
                module,
                model,
                neural_config,
                baseline_n_playout,
                baseline_c_puct,
                candidate_first=False,
                seed=seed + games_per_side * 101 + offset * 101,
            )
            _record_match_result(records, False, winner, moves)

    wins = sum(item["candidate_winner"] == 1 for item in records)
    losses = sum(item["candidate_winner"] == 2 for item in records)
    draws = sum(item["candidate_winner"] == 0 for item in records)
    total = len(records)
    score = wins + 0.5 * draws
    return {
        "rule_version": RULE_VERSION,
        "baseline_n_playout": baseline_n_playout,
        "baseline_c_puct": baseline_c_puct,
        "neural_simulations": neural_config.simulations,
        "neural_c_puct": neural_config.c_puct,
        "games_per_side": games_per_side,
        "seeds": seed_list,
        "candidate_wins": wins,
        "candidate_losses": losses,
        "draws": draws,
        "total_games": total,
        "score_rate": score / total if total else 0.0,
        "win_rate": wins / total if total else 0.0,
        "records": records,
    }
