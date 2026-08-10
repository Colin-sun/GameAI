from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np
import torch

from scripts.alphazero_torch import (
    ACTION_SIZE,
    INPUT_SIZE,
    GameData,
    TorchPolicyValueNetwork,
    augment_symmetries,
    sharpen_policy_targets,
    train_network,
)
from scripts.alphazero import NeuralMCTS, NeuralSearchConfig
from scripts.common import load_native_module


GAMEAI_NATIVE = load_native_module()


class TorchAlphaZeroTests(unittest.TestCase):
    def test_native_mcts_accepts_policy_priors(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe()
        priors = np.zeros(ACTION_SIZE, dtype=np.float32)
        priors[0] = 1.0
        engine = GAMEAI_NATIVE.MCTSPure(
            8,
            0.3,
            seed=12,
            rollout_policy=1,
            rollout_limit=8,
        )
        action = engine.get_move_with_priors(game, priors.tolist(), [1], True)
        self.assertEqual(action, 1)

    def test_network_predict_and_checkpoint_round_trip(self) -> None:
        model = TorchPolicyValueNetwork(channels=8, blocks=1)
        state = np.linspace(0.0, 1.0, INPUT_SIZE, dtype=np.float32)
        policy, value = model.predict(state)
        self.assertEqual(policy.shape, (ACTION_SIZE,))
        self.assertAlmostEqual(float(policy.sum()), 1.0, places=5)
        self.assertTrue(np.isfinite(value))

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "model.pt"
            model.save(path)
            restored = TorchPolicyValueNetwork.load(path, device="cpu")
        actual_policy, actual_value = restored.predict(state)
        np.testing.assert_allclose(actual_policy, policy, rtol=1.0e-5, atol=1.0e-5)
        self.assertAlmostEqual(actual_value, value, places=5)

    def test_symmetry_augmentation_produces_eight_positions(self) -> None:
        states = np.zeros((1, INPUT_SIZE), dtype=np.float32)
        states[0, 0] = 1.0
        policies = np.zeros((1, ACTION_SIZE), dtype=np.float32)
        policies[0, 0] = 1.0
        data = GameData(states, policies, np.asarray([1.0], dtype=np.float32), [1], [1])
        augmented = augment_symmetries(data)
        self.assertEqual(augmented.positions, 8)
        np.testing.assert_allclose(augmented.policies.sum(axis=1), 1.0)
        self.assertEqual(np.count_nonzero(augmented.states[:, 0]), 2)

    def test_policy_target_sharpening_and_hard_targets(self) -> None:
        states = np.zeros((1, INPUT_SIZE), dtype=np.float32)
        policies = np.zeros((1, ACTION_SIZE), dtype=np.float32)
        policies[0, 0] = 0.8
        policies[0, 1] = 0.2
        data = GameData(states, policies, np.asarray([0.0], dtype=np.float32), [], [])
        sharpened = sharpen_policy_targets(data, exponent=2.0)
        self.assertGreater(float(sharpened.policies[0, 0]), 0.9)
        hard = sharpen_policy_targets(data, hard=True)
        np.testing.assert_array_equal(hard.policies[0], np.eye(ACTION_SIZE, dtype=np.float32)[0])

    def test_cpu_training_updates_network(self) -> None:
        rng = np.random.default_rng(42)
        states = rng.random((16, INPUT_SIZE), dtype=np.float32)
        policies = np.zeros((16, ACTION_SIZE), dtype=np.float32)
        policies[np.arange(16), np.arange(16)] = 1.0
        values = np.linspace(-1.0, 1.0, 16, dtype=np.float32)
        data = GameData(states, policies, values, [], [])
        model = TorchPolicyValueNetwork(channels=8, blocks=1)
        before = model.policy_fc.weight.detach().clone()
        metrics = train_network(
            model,
            data,
            device=torch.device("cpu"),
            epochs=1,
            batch_size=8,
            learning_rate=0.001,
            weight_decay=0.0,
            seed=7,
        )
        self.assertEqual(len(metrics), 1)
        self.assertTrue(np.isfinite(metrics[0]["loss"]))
        self.assertFalse(torch.equal(before, model.policy_fc.weight.detach()))

    def test_batched_search_uses_torch_model(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe()
        model = TorchPolicyValueNetwork(channels=8, blocks=1)
        search = NeuralMCTS(
            model,
            NeuralSearchConfig(simulations=8, inference_batch_size=4),
            seed=9,
        )
        search.search(game)
        self.assertTrue(game.is_action_valid(search.choose_action()))


if __name__ == "__main__":
    unittest.main()
