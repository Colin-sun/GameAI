#!/usr/bin/env python3
"""CUDA policy/value training and CPU-parallel data generation for UTT.

The native tactical MCTS remains the reference player.  This module adds a
larger residual policy/value network and a deliberately separate data path:
CPU worker processes run native self-play, while the parent process owns the
CUDA model and performs batched optimization.
"""

from __future__ import annotations

import os
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from multiprocessing import get_context
from pathlib import Path
from typing import Any, Iterable

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from torch.utils.data import DataLoader, TensorDataset

try:
    from alphazero import (
        ACTION_SIZE,
        BOARD_SIZE,
        INPUT_PLANES,
        INPUT_SIZE,
        NeuralMCTS,
        NeuralSearchConfig,
        TrainingExample,
        encode_state,
        legal_actions,
        masked_policy,
        outcome_for_player,
        play_self_play_game,
        play_teacher_game,
        tactical_policy,
    )
    from common import load_native_module
except ImportError:  # pragma: no cover - supports package-style imports
    from scripts.alphazero import (
        ACTION_SIZE,
        BOARD_SIZE,
        INPUT_PLANES,
        INPUT_SIZE,
        NeuralMCTS,
        NeuralSearchConfig,
        TrainingExample,
        encode_state,
        legal_actions,
        masked_policy,
        outcome_for_player,
        play_self_play_game,
        play_teacher_game,
        tactical_policy,
    )
    from scripts.common import load_native_module


RULE_VERSION = "majority-utt-v1"
NETWORK_FORMAT_VERSION = 1


@dataclass
class GameData:
    """Dense arrays returned by one or more CPU game workers."""

    states: np.ndarray
    policies: np.ndarray
    values: np.ndarray
    winners: list[int]
    moves: list[int]

    @property
    def positions(self) -> int:
        return int(self.states.shape[0])


def _empty_data() -> GameData:
    return GameData(
        np.empty((0, INPUT_SIZE), dtype=np.float32),
        np.empty((0, ACTION_SIZE), dtype=np.float32),
        np.empty((0,), dtype=np.float32),
        [],
        [],
    )


def _pack_examples(
    examples: list[TrainingExample], winner: int, moves: int
) -> GameData:
    if not examples:
        return GameData(
            np.empty((0, INPUT_SIZE), dtype=np.float32),
            np.empty((0, ACTION_SIZE), dtype=np.float32),
            np.empty((0,), dtype=np.float32),
            [int(winner)],
            [int(moves)],
        )
    return GameData(
        np.ascontiguousarray(np.stack([item.state for item in examples]), dtype=np.float32),
        np.ascontiguousarray(np.stack([item.policy for item in examples]), dtype=np.float32),
        np.asarray([item.value for item in examples], dtype=np.float32),
        [int(winner)],
        [int(moves)],
    )


def combine_game_data(chunks: Iterable[GameData]) -> GameData:
    chunks = list(chunks)
    if not chunks:
        return _empty_data()
    return GameData(
        np.ascontiguousarray(np.concatenate([chunk.states for chunk in chunks], axis=0)),
        np.ascontiguousarray(np.concatenate([chunk.policies for chunk in chunks], axis=0)),
        np.ascontiguousarray(np.concatenate([chunk.values for chunk in chunks], axis=0)),
        [winner for chunk in chunks for winner in chunk.winners],
        [moves for chunk in chunks for moves in chunk.moves],
    )


def sharpen_policy_targets(
    data: GameData,
    exponent: float = 1.0,
    hard: bool = False,
) -> GameData:
    """Sharpen teacher visit targets without changing states or values."""

    if exponent <= 0.0 or not np.isfinite(exponent):
        raise ValueError("policy target exponent must be finite and positive")
    if data.positions == 0:
        return data
    if hard:
        policies = np.zeros_like(data.policies, dtype=np.float32)
        policies[np.arange(data.positions), np.argmax(data.policies, axis=1)] = 1.0
    elif exponent == 1.0:
        policies = np.ascontiguousarray(data.policies, dtype=np.float32)
    else:
        adjusted = np.power(np.maximum(data.policies, 0.0), float(exponent))
        totals = adjusted.sum(axis=1, keepdims=True)
        fallback = totals[:, 0] <= 0.0
        adjusted /= np.maximum(totals, 1.0e-12)
        if np.any(fallback):
            adjusted[fallback] = data.policies[fallback]
        policies = np.ascontiguousarray(adjusted, dtype=np.float32)
    return GameData(
        np.ascontiguousarray(data.states),
        policies,
        np.ascontiguousarray(data.values, dtype=np.float32),
        list(data.winners),
        list(data.moves),
    )


def _worker_init() -> None:
    """Keep native/Torch worker processes from oversubscribing CPU cores."""

    os.environ["OMP_NUM_THREADS"] = "1"
    os.environ["MKL_NUM_THREADS"] = "1"
    os.environ["OPENBLAS_NUM_THREADS"] = "1"
    torch.set_num_threads(1)
    torch.set_num_interop_threads(1)


def _teacher_worker(task: tuple[int, int, float, float]) -> GameData:
    seed, n_playout, c_puct, tree_value_weight = task
    module = load_native_module()
    examples, winner, moves = play_teacher_game(
        module,
        n_playout=int(n_playout),
        c_puct=float(c_puct),
        seed=int(seed),
        tree_value_weight=float(tree_value_weight),
    )
    return _pack_examples(examples, winner, moves)


def generate_teacher_data(
    games: int,
    n_playout: int,
    c_puct: float,
    seed: int,
    workers: int,
    tree_value_weight: float = 0.35,
) -> GameData:
    """Generate tactical-MCTS policy/value targets in CPU processes."""

    if games < 0 or workers <= 0 or n_playout <= 0:
        raise ValueError("games/workers/playout must be positive or games may be zero")
    if games == 0:
        return _empty_data()
    if not 0.0 <= tree_value_weight <= 1.0:
        raise ValueError("tree_value_weight must be in [0, 1]")
    tasks = [
        (
            int(seed) + game_index * 1009,
            int(n_playout),
            float(c_puct),
            float(tree_value_weight),
        )
        for game_index in range(games)
    ]
    context = get_context("spawn")
    with ProcessPoolExecutor(
        max_workers=min(int(workers), games),
        mp_context=context,
        initializer=_worker_init,
    ) as executor:
        chunks = list(executor.map(_teacher_worker, tasks, chunksize=1))
    return combine_game_data(chunks)


def _self_play_worker(
    task: tuple[str, int, dict[str, Any], int]
) -> GameData:
    checkpoint, seed, config_data, temperature_moves = task
    module = load_native_module()
    model = TorchPolicyValueNetwork.load(Path(checkpoint), device="cpu")
    config = NeuralSearchConfig(**config_data)
    examples, winner, moves = play_self_play_game(
        module,
        model,
        config,
        seed=int(seed),
        temperature_moves=int(temperature_moves),
    )
    return _pack_examples(examples, winner, moves)


def generate_neural_self_play_data(
    checkpoint: Path,
    games: int,
    config: NeuralSearchConfig,
    seed: int,
    workers: int,
    temperature_moves: int,
) -> GameData:
    """Generate AlphaZero self-play positions with CPU worker-side inference."""

    if games < 0 or workers <= 0:
        raise ValueError("games/workers must be positive or games may be zero")
    if games == 0:
        return _empty_data()
    checkpoint = Path(checkpoint).resolve()
    config_data = {
        "simulations": config.simulations,
        "c_puct": config.c_puct,
        "dirichlet_alpha": config.dirichlet_alpha,
        "dirichlet_epsilon": config.dirichlet_epsilon,
        "tactical_prior_weight": config.tactical_prior_weight,
        "rollout_value_weight": config.rollout_value_weight,
        "rollout_limit": config.rollout_limit,
        "force_tactical": config.force_tactical,
        "inference_batch_size": config.inference_batch_size,
        "policy_exponent": config.policy_exponent,
        "root_selection": config.root_selection,
    }
    tasks = [
        (
            str(checkpoint),
            int(seed) + game_index * 2003,
            config_data,
            int(temperature_moves),
        )
        for game_index in range(games)
    ]
    context = get_context("spawn")
    with ProcessPoolExecutor(
        max_workers=min(int(workers), games),
        mp_context=context,
        initializer=_worker_init,
    ) as executor:
        chunks = list(executor.map(_self_play_worker, tasks, chunksize=1))
    return combine_game_data(chunks)


def augment_symmetries(data: GameData, max_positions: int = 0, seed: int = 0) -> GameData:
    """Apply all eight square symmetries to state and policy targets."""

    if data.positions == 0:
        return data
    states = data.states
    policies = data.policies
    values = data.values
    if max_positions > 0 and data.positions > max_positions:
        rng = np.random.default_rng(seed)
        selected = np.sort(rng.choice(data.positions, max_positions, replace=False))
        states = states[selected]
        policies = policies[selected]
        values = values[selected]

    state_planes = states.reshape(-1, INPUT_PLANES, BOARD_SIZE, BOARD_SIZE)
    policy_grid = policies.reshape(-1, BOARD_SIZE, BOARD_SIZE)
    state_parts: list[np.ndarray] = []
    policy_parts: list[np.ndarray] = []
    value_parts: list[np.ndarray] = []
    for flip in (False, True):
        for rotation in range(4):
            transformed_states = np.rot90(state_planes, rotation, axes=(2, 3))
            transformed_policies = np.rot90(policy_grid, rotation, axes=(1, 2))
            if flip:
                transformed_states = np.flip(transformed_states, axis=3)
                transformed_policies = np.flip(transformed_policies, axis=2)
            state_parts.append(np.ascontiguousarray(transformed_states.reshape(-1, INPUT_SIZE)))
            policy_parts.append(np.ascontiguousarray(transformed_policies.reshape(-1, ACTION_SIZE)))
            value_parts.append(np.asarray(values, dtype=np.float32))
    return GameData(
        np.concatenate(state_parts, axis=0),
        np.concatenate(policy_parts, axis=0),
        np.concatenate(value_parts, axis=0),
        list(data.winners),
        list(data.moves),
    )


class ResidualBlock(nn.Module):
    def __init__(self, channels: int) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, kernel_size=3, padding=1, bias=True)
        self.conv2 = nn.Conv2d(channels, channels, kernel_size=3, padding=1, bias=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        residual = x
        x = F.relu(self.conv1(x), inplace=False)
        x = self.conv2(x)
        return F.relu(x + residual, inplace=False)


class TorchPolicyValueNetwork(nn.Module):
    """Residual policy/value network with a NumPy-compatible inference method."""

    format_version = NETWORK_FORMAT_VERSION

    def __init__(self, channels: int = 128, blocks: int = 8) -> None:
        super().__init__()
        if channels <= 0 or blocks <= 0:
            raise ValueError("channels and blocks must be positive")
        self.channels = int(channels)
        self.blocks = int(blocks)
        self.stem = nn.Conv2d(INPUT_PLANES, channels, kernel_size=3, padding=1)
        self.residual = nn.Sequential(*(ResidualBlock(channels) for _ in range(blocks)))
        self.policy_conv = nn.Conv2d(channels, 32, kernel_size=1)
        self.policy_fc = nn.Linear(32 * ACTION_SIZE, ACTION_SIZE)
        self.value_conv = nn.Conv2d(channels, 32, kernel_size=1)
        self.value_fc1 = nn.Linear(32 * ACTION_SIZE, 128)
        self.value_fc2 = nn.Linear(128, 1)

    def _input_tensor(self, states: torch.Tensor) -> torch.Tensor:
        if states.ndim == 2:
            if states.shape[1] != INPUT_SIZE:
                raise ValueError(f"flat states must have shape (batch, {INPUT_SIZE})")
            states = states.reshape(-1, INPUT_PLANES, BOARD_SIZE, BOARD_SIZE)
        elif states.ndim == 4:
            expected = (INPUT_PLANES, BOARD_SIZE, BOARD_SIZE)
            if tuple(states.shape[1:]) != expected:
                raise ValueError(f"image states must have shape (batch, {expected})")
        else:
            raise ValueError("states must be flat or image tensors")
        return states.float()

    def forward(self, states: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        x = self._input_tensor(states)
        x = F.relu(self.stem(x), inplace=False)
        x = self.residual(x)

        policy = F.relu(self.policy_conv(x), inplace=False).flatten(1)
        policy_logits = self.policy_fc(policy)

        value = F.relu(self.value_conv(x), inplace=False).flatten(1)
        value = F.relu(self.value_fc1(value), inplace=False)
        value = torch.tanh(self.value_fc2(value)).squeeze(1)
        return policy_logits, value

    @torch.inference_mode()
    def predict(self, state: np.ndarray) -> tuple[np.ndarray, float]:
        values = np.asarray(state, dtype=np.float32)
        single = values.ndim == 1
        if single:
            values = values[None, :]
        tensor = torch.as_tensor(values, device=next(self.parameters()).device)
        self.eval()
        logits, value = self(tensor)
        policy = torch.softmax(logits, dim=1)
        if single:
            return (
                policy[0].detach().cpu().numpy().astype(np.float32),
                float(value[0].detach().cpu()),
            )
        return (
            policy.detach().cpu().numpy().astype(np.float32),
            value.detach().cpu().numpy().astype(np.float32),
        )

    def save(self, path: Path, extra: dict[str, Any] | None = None) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        metadata: dict[str, Any] = {
            "format_version": self.format_version,
            "rule_version": RULE_VERSION,
            "input_planes": INPUT_PLANES,
            "input_size": INPUT_SIZE,
            "action_size": ACTION_SIZE,
            "channels": self.channels,
            "blocks": self.blocks,
        }
        if extra:
            metadata.update(extra)
        torch.save({"metadata": metadata, "model_state": self.state_dict()}, path)

    @classmethod
    def load(cls, path: Path, device: str | torch.device = "cpu") -> "TorchPolicyValueNetwork":
        checkpoint = torch.load(Path(path), map_location=device)
        metadata = checkpoint.get("metadata", {})
        if metadata.get("rule_version") != RULE_VERSION:
            raise ValueError(
                f"checkpoint rule {metadata.get('rule_version')!r} != {RULE_VERSION!r}"
            )
        model = cls(
            channels=int(metadata["channels"]),
            blocks=int(metadata["blocks"]),
        )
        model.load_state_dict(checkpoint["model_state"])
        model.to(device)
        model.eval()
        return model


def train_network(
    model: TorchPolicyValueNetwork,
    data: GameData,
    device: str | torch.device,
    epochs: int,
    batch_size: int,
    learning_rate: float,
    weight_decay: float,
    seed: int,
    policy_weight: float = 1.0,
    value_weight: float = 1.0,
) -> list[dict[str, float]]:
    """Run batched GPU training and return per-epoch losses."""

    if data.positions == 0:
        raise ValueError("cannot train on an empty dataset")
    if epochs < 0 or batch_size <= 0 or learning_rate <= 0.0:
        raise ValueError("epochs/batch_size/learning_rate are invalid")
    device = torch.device(device)
    model.to(device)
    model.train()
    torch.manual_seed(int(seed))
    if device.type == "cuda":
        torch.cuda.manual_seed_all(int(seed))
        torch.set_float32_matmul_precision("high")

    states = np.ascontiguousarray(data.states.reshape(-1, INPUT_PLANES, BOARD_SIZE, BOARD_SIZE))
    policies = np.ascontiguousarray(data.policies, dtype=np.float32)
    values = np.ascontiguousarray(data.values, dtype=np.float32)
    dataset = TensorDataset(
        torch.from_numpy(states),
        torch.from_numpy(policies),
        torch.from_numpy(values),
    )
    loader = DataLoader(
        dataset,
        batch_size=int(batch_size),
        shuffle=True,
        generator=torch.Generator().manual_seed(int(seed)),
        num_workers=0,
        pin_memory=device.type == "cuda",
    )
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=float(learning_rate), weight_decay=float(weight_decay)
    )
    metrics: list[dict[str, float]] = []
    for epoch in range(int(epochs)):
        policy_total = 0.0
        value_total = 0.0
        loss_total = 0.0
        count = 0
        for batch_states, batch_policies, batch_values in loader:
            batch_states = batch_states.to(device, non_blocking=True)
            batch_policies = batch_policies.to(device, non_blocking=True)
            batch_values = batch_values.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            logits, predicted_values = model(batch_states)
            policy_loss = -(
                batch_policies * F.log_softmax(logits, dim=1)
            ).sum(dim=1).mean()
            value_loss = F.mse_loss(predicted_values, batch_values)
            loss = float(policy_weight) * policy_loss + float(value_weight) * value_loss
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=5.0)
            optimizer.step()
            size = int(batch_states.shape[0])
            count += size
            policy_total += float(policy_loss.detach()) * size
            value_total += float(value_loss.detach()) * size
            loss_total += float(loss.detach()) * size
        metrics.append(
            {
                "epoch": float(epoch + 1),
                "loss": loss_total / count,
                "policy_loss": policy_total / count,
                "value_loss": value_total / count,
                "positions": float(count),
            }
        )
    model.eval()
    if device.type == "cuda":
        torch.cuda.synchronize(device)
    return metrics


def evaluate_torch_model(
    module: Any,
    model: TorchPolicyValueNetwork,
    games_per_side: int,
    seeds: Iterable[int],
    neural_config: NeuralSearchConfig,
    baseline_n_playout: int,
    baseline_c_puct: float,
    candidate_mode: str = "neural",
) -> dict[str, Any]:
    """Evaluate the Torch model through the existing neural-MCTS match path."""

    if candidate_mode == "native-prior":
        return evaluate_native_prior_model(
            module,
            model,
            games_per_side,
            seeds,
            neural_config,
            baseline_n_playout,
            baseline_c_puct,
        )
    if candidate_mode != "neural":
        raise ValueError(f"unknown candidate mode: {candidate_mode}")

    try:
        from alphazero import evaluate_model_against_mcts
    except ImportError:  # pragma: no cover - supports package-style imports
        from scripts.alphazero import evaluate_model_against_mcts

    return evaluate_model_against_mcts(
        module,
        model,
        games_per_side=int(games_per_side),
        seeds=[int(seed) for seed in seeds],
        neural_config=neural_config,
        baseline_n_playout=int(baseline_n_playout),
        baseline_c_puct=float(baseline_c_puct),
    )


def _model_prior(
    model: TorchPolicyValueNetwork,
    game: Any,
    config: NeuralSearchConfig,
) -> tuple[np.ndarray, list[int] | None]:
    policy, _ = model.predict(encode_state(game))
    policy = np.power(np.maximum(policy, 0.0), config.policy_exponent)
    actions = legal_actions(game)
    tactical = tactical_policy(game, actions)
    allowed_actions: list[int] | None = None
    if config.tactical_prior_weight > 0.0:
        weight = config.tactical_prior_weight
        policy = (1.0 - weight) * policy + weight * tactical
    if config.force_tactical:
        tactical_actions = np.flatnonzero(tactical > 0.0)
        if 0 < len(tactical_actions) < len(actions):
            allowed_actions = [int(action) for action in tactical_actions]
            allowed = set(allowed_actions)
            policy = np.asarray(
                [
                    float(policy[action]) if action in allowed else 0.0
                    for action in range(ACTION_SIZE)
                ],
                dtype=np.float32,
            )
    return masked_policy(policy, actions), allowed_actions


def _record_native_prior_result(
    records: list[dict[str, Any]], candidate_first: bool, winner: int, moves: int
) -> None:
    if int(winner) == -1:
        candidate_winner = 0
    elif (int(winner) == 1) == candidate_first:
        candidate_winner = 1
    else:
        candidate_winner = 2
    records.append(
        {
            "candidate_first": bool(candidate_first),
            "candidate_winner": candidate_winner,
            "winner": int(winner),
            "moves": int(moves),
        }
    )


def _play_native_prior_game(
    module: Any,
    model: TorchPolicyValueNetwork,
    neural_config: NeuralSearchConfig,
    baseline_n_playout: int,
    baseline_c_puct: float,
    candidate_first: bool,
    seed: int,
) -> tuple[int, int]:
    candidate_player = 1 if candidate_first else 2
    candidate = module.MCTSPure(
        n_playout=neural_config.simulations,
        c_puct=neural_config.c_puct,
        seed=(int(seed) + 7) & 0xFFFFFFFF,
        rollout_policy=1,
        rollout_limit=neural_config.rollout_limit,
    )
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
            raise RuntimeError("native-prior evaluation exceeded the maximum move count")
        current_player = int(game.get_current_player())
        if current_player == candidate_player:
            prior, allowed_actions = _model_prior(model, game, neural_config)
            action = int(
                candidate.get_move_with_priors(
                    game,
                    prior.tolist(),
                    allowed_actions or [],
                    neural_config.root_selection == "q",
                )
            )
            baseline.update_with_move(action)
        else:
            action = int(baseline.get_move(game))
            candidate.update_with_move(action)
        if not game.make_move(action):
            raise RuntimeError(f"native-prior evaluation produced invalid action {action}")
        moves += 1


def evaluate_native_prior_model(
    module: Any,
    model: TorchPolicyValueNetwork,
    games_per_side: int,
    seeds: Iterable[int],
    neural_config: NeuralSearchConfig,
    baseline_n_playout: int,
    baseline_c_puct: float,
) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    seed_list = [int(seed) for seed in seeds]
    for seed in seed_list:
        for offset in range(games_per_side):
            winner, moves = _play_native_prior_game(
                module,
                model,
                neural_config,
                baseline_n_playout,
                baseline_c_puct,
                True,
                seed + offset * 101,
            )
            _record_native_prior_result(records, True, winner, moves)
        for offset in range(games_per_side):
            winner, moves = _play_native_prior_game(
                module,
                model,
                neural_config,
                baseline_n_playout,
                baseline_c_puct,
                False,
                seed + games_per_side * 101 + offset * 101,
            )
            _record_native_prior_result(records, False, winner, moves)

    wins = sum(item["candidate_winner"] == 1 for item in records)
    losses = sum(item["candidate_winner"] == 2 for item in records)
    draws = sum(item["candidate_winner"] == 0 for item in records)
    total = len(records)
    score = wins + 0.5 * draws
    return {
        "rule_version": RULE_VERSION,
        "candidate_mode": "native-prior",
        "baseline_n_playout": int(baseline_n_playout),
        "baseline_c_puct": float(baseline_c_puct),
        "neural_simulations": int(neural_config.simulations),
        "neural_c_puct": float(neural_config.c_puct),
        "neural_tactical_prior_weight": float(neural_config.tactical_prior_weight),
        "neural_rollout_value_weight": float(neural_config.rollout_value_weight),
        "neural_rollout_limit": int(neural_config.rollout_limit),
        "neural_policy_exponent": float(neural_config.policy_exponent),
        "neural_force_tactical": bool(neural_config.force_tactical),
        "neural_inference_batch_size": int(neural_config.inference_batch_size),
        "neural_root_selection": str(neural_config.root_selection),
        "games_per_side": int(games_per_side),
        "seeds": seed_list,
        "candidate_wins": wins,
        "candidate_losses": losses,
        "draws": draws,
        "total_games": total,
        "score_rate": score / total if total else 0.0,
        "win_rate": wins / total if total else 0.0,
        "records": records,
    }
