from __future__ import annotations

import unittest

from scripts.common import load_native_module


GAMEAI_NATIVE = load_native_module()


def board_with_meta_states(states: list[list[int]]) -> list[list[int]]:
    board = [[0] * 9 for _ in range(9)]
    drawn_sub_board = [
        [1, 2, 1],
        [1, 2, 2],
        [2, 1, 2],
    ]
    for meta_row, row in enumerate(states):
        for meta_col, state in enumerate(row):
            base_row = meta_row * 3
            base_col = meta_col * 3
            if state in (1, 2):
                board[base_row][base_col : base_col + 3] = [state] * 3
            elif state == 3:
                for local_row in range(3):
                    board[base_row + local_row][base_col : base_col + 3] = drawn_sub_board[local_row]
    return board


class NativeMCTSTests(unittest.TestCase):
    def test_meta_majority_ends_game_and_blocks_moves(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe.from_board(
            board_with_meta_states([[1, 1, 1], [1, 1, 0], [0, 0, 0]]), (-1, -1)
        )
        self.assertEqual(game.get_meta_board()[0], [1, 1, 1])
        self.assertEqual(game.get_done_winner(), (True, 1))
        self.assertEqual(game.get_valid_actions(), [])
        self.assertFalse(game.make_move(40))

    def test_meta_line_alone_does_not_end_game(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe.from_board(
            board_with_meta_states([[1, 1, 1], [0, 0, 0], [0, 0, 0]]), (-1, -1)
        )
        self.assertEqual(game.get_done_winner(), (False, 0))

    def test_finished_meta_board_counts_decide_winner(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe.from_board(
            board_with_meta_states([[1, 1, 2], [1, 1, 2], [3, 2, 3]]), (-1, -1)
        )
        self.assertEqual(game.get_done_winner(), (True, 1))

    def test_drawn_meta_boards_are_excluded_from_majority_threshold(self) -> None:
        game = GAMEAI_NATIVE.UltimateTicTacToe.from_board(
            board_with_meta_states([[1, 1, 1], [1, 3, 3], [0, 0, 0]]), (-1, -1)
        )
        self.assertEqual(game.get_done_winner(), (True, 1))

    def test_draw_and_undo_restore_state(self) -> None:
        board = board_with_meta_states([[3, 3, 3], [3, 3, 3], [3, 3, 3]])
        game = GAMEAI_NATIVE.UltimateTicTacToe.from_board(board, (-1, -1))
        self.assertEqual(game.get_done_winner(), (True, -1))

        empty = GAMEAI_NATIVE.UltimateTicTacToe()
        self.assertTrue(empty.make_move(0))
        self.assertEqual(empty.get_next_board(), (0, 0))
        empty.undo_move_rc(0, 0)
        self.assertEqual(empty.get_board(), [[0] * 9 for _ in range(9)])
        self.assertEqual(empty.get_next_board(), (-1, -1))
        self.assertEqual(empty.get_current_player(), 1)
        self.assertEqual(empty.get_step(), 0)

    def test_seed_reproduces_first_move(self) -> None:
        board = GAMEAI_NATIVE.UltimateTicTacToe()
        first = GAMEAI_NATIVE.MCTSPure(40, 0.8, seed=1234, rollout_policy=1, rollout_limit=32)
        second = GAMEAI_NATIVE.MCTSPure(40, 0.8, seed=1234, rollout_policy=1, rollout_limit=32)
        self.assertEqual(first.get_move(board.clone()), second.get_move(board.clone()))

    def test_detailed_matches_return_balanced_wdl(self) -> None:
        summary = GAMEAI_NATIVE.compare_mcts_detailed(
            20,
            0.8,
            20,
            0.8,
            games_per_side=1,
            seed=99,
            rollout_policy1=1,
            rollout_limit1=32,
            rollout_policy2=0,
            rollout_limit2=300,
        )
        self.assertEqual(summary["total_games"], 2)
        self.assertEqual(
            summary["candidate1_wins"] + summary["candidate2_wins"] + summary["draws"],
            2,
        )
        self.assertEqual(len(summary["games"]), 2)


if __name__ == "__main__":
    unittest.main()
