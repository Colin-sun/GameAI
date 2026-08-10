from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

from scripts.alphazero import (
    ACTION_SIZE,
    INPUT_SIZE,
    NeuralMCTS,
    NeuralSearchConfig,
    PolicyValueNetwork,
    SearchNode,
    encode_state,
    masked_policy,
)
from scripts.common import load_native_module


GAMEAI_NATIVE = load_native_module()


class AlphaZeroToolsTests(unittest.TestCase):
    def test_encode_state_and_mask_follow_native_legal_actions(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe()
        encoded = encode_state(game)
        self.assertEqual(encoded.shape, (INPUT_SIZE,))
        self.assertTrue(np.isfinite(encoded).all())

        policy = np.arange(ACTION_SIZE, dtype=np.float32)
        masked = masked_policy(policy, [0, 2, 4])
        self.assertAlmostEqual(float(masked.sum()), 1.0)
        self.assertEqual(float(masked[1]), 0.0)
        self.assertGreater(float(masked[4]), float(masked[2]))

    def test_neural_mcts_returns_a_legal_action(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe()
        model = PolicyValueNetwork(hidden_size=16, seed=4)
        search = NeuralMCTS(model, NeuralSearchConfig(simulations=4), seed=5)
        search.search(game)
        action = search.choose_action()
        self.assertTrue(game.is_action_valid(action))
        self.assertAlmostEqual(float(search.visit_policy().sum()), 1.0)

    def test_batched_neural_mcts_returns_a_legal_action(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe()
        model = PolicyValueNetwork(hidden_size=16, seed=14)
        search = NeuralMCTS(
            model,
            NeuralSearchConfig(simulations=8, inference_batch_size=4),
            seed=15,
        )
        search.search(game)
        self.assertTrue(game.is_action_valid(search.choose_action()))

    def test_backup_stores_child_value_from_parent_perspective(self) -> None:
        root = SearchNode()
        child = SearchNode()
        NeuralMCTS._backup([root, child], -1.0)
        self.assertEqual(child.visit_count, 1)
        self.assertEqual(root.visit_count, 1)
        self.assertEqual(child.q, -1.0)
        self.assertEqual(root.q, 1.0)

    def test_checkpoint_round_trip(self) -> None:
        model = PolicyValueNetwork(hidden_size=16, seed=8)
        state = np.linspace(0.0, 1.0, INPUT_SIZE, dtype=np.float32)
        expected_policy, expected_value = model.predict(state)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "model.npz"
            model.save(path)
            restored = PolicyValueNetwork.load(path)
        actual_policy, actual_value = restored.predict(state)
        np.testing.assert_allclose(actual_policy, expected_policy, rtol=1.0e-6, atol=1.0e-6)
        self.assertAlmostEqual(actual_value, expected_value, places=6)


if __name__ == "__main__":
    unittest.main()
