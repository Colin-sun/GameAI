#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "mcts_pure.h"
#include "utt.h"

namespace py = pybind11;

namespace {

std::vector<std::vector<int>> board_to_vector(
    const std::array<std::array<int, BOARD_SIZE>, BOARD_SIZE>& board) {
    std::vector<std::vector<int>> result(BOARD_SIZE, std::vector<int>(BOARD_SIZE));
    for (int row = 0; row < BOARD_SIZE; ++row) {
        for (int col = 0; col < BOARD_SIZE; ++col) {
            result[row][col] = board[row][col];
        }
    }
    return result;
}

std::vector<std::vector<int>> meta_board_to_vector(
    const std::array<std::array<int, META_BOARD_SIZE>, META_BOARD_SIZE>& meta_board) {
    std::vector<std::vector<int>> result(META_BOARD_SIZE, std::vector<int>(META_BOARD_SIZE));
    for (int row = 0; row < META_BOARD_SIZE; ++row) {
        for (int col = 0; col < META_BOARD_SIZE; ++col) {
            result[row][col] = meta_board[row][col];
        }
    }
    return result;
}

py::dict search_tree_snapshot_to_dict(const SearchTreeSnapshot& snapshot) {
    py::list nodes;
    for (const auto& node : snapshot.nodes) {
        py::dict item;
        item["id"] = node.id;
        item["parent_id"] = node.parent_id;
        item["action"] = node.action;
        item["visits"] = node.visits;
        item["q"] = node.q;
        item["u"] = node.u;
        item["prior"] = node.prior;
        item["value"] = node.value;
        nodes.append(item);
    }

    py::dict result;
    result["selected_action"] = snapshot.selected_action;
    result["root_visits"] = snapshot.root_visits;
    result["node_count"] = snapshot.node_count;
    result["nodes"] = nodes;
    return result;
}

struct GameResult {
    int winner;
    int moves;
};

struct MatchGameResult {
    bool candidate1_first;
    int candidate_winner;
    int moves;
};

struct MatchSummary {
    int candidate1_wins = 0;
    int candidate2_wins = 0;
    int draws = 0;
    std::vector<MatchGameResult> games;
};

GameResult play_game(
    int n_playout_player_one,
    float c_puct_player_one,
    unsigned int seed_player_one,
    int rollout_policy_player_one,
    int rollout_limit_player_one,
    int n_playout_player_two,
    float c_puct_player_two,
    unsigned int seed_player_two,
    int rollout_policy_player_two,
    int rollout_limit_player_two) {
    std::mt19937 rng_player_one(seed_player_one);
    std::mt19937 rng_player_two(seed_player_two);
    MCTSPure<UltimateTicTacToe, ActionList> player_one(
        n_playout_player_one,
        c_puct_player_one,
        rng_player_one,
        false,
        rollout_policy_player_one,
        rollout_limit_player_one);
    MCTSPure<UltimateTicTacToe, ActionList> player_two(
        n_playout_player_two,
        c_puct_player_two,
        rng_player_two,
        false,
        rollout_policy_player_two,
        rollout_limit_player_two);

    UltimateTicTacToe game;
    std::pair<bool, int> done_winner;
    int moves = 0;
    while (!(done_winner = game.get_done_winner()).first) {
        if (moves >= MAX_MOVES) {
            throw std::runtime_error("Self-play exceeded the maximum move count");
        }
        int action = -1;
        if (game.get_current_player() == 1) {
            action = player_one.get_move(game);
            player_two.update_with_move(action);
        } else {
            action = player_two.get_move(game);
            player_one.update_with_move(action);
        }
        if (action == -1 || !game.make_move(action)) {
            throw std::runtime_error("MCTS produced an invalid move during self-play");
        }
        ++moves;
    }
    return GameResult{done_winner.second, moves};
}

unsigned int mix_seed(unsigned int base_seed, int offset) {
    auto mixed = static_cast<std::uint64_t>(base_seed) + 0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(offset);
    mixed ^= mixed >> 30;
    mixed *= 0xbf58476d1ce4e5b9ULL;
    mixed ^= mixed >> 27;
    mixed *= 0x94d049bb133111ebULL;
    mixed ^= mixed >> 31;
    return static_cast<unsigned int>(mixed);
}

MatchSummary compare_mcts_summary(
    int n_playout1,
    float c_puct1,
    int n_playout2,
    float c_puct2,
    int games_per_side,
    unsigned int seed,
    int rollout_policy1,
    int rollout_limit1,
    int rollout_policy2,
    int rollout_limit2) {
    if (games_per_side <= 0) {
        throw std::runtime_error("games_per_side must be positive");
    }

    MatchSummary summary;
    summary.games.reserve(games_per_side * 2);

    auto record_game = [&summary](const GameResult& result, bool candidate1_first) {
        int candidate_winner = 0;
        if (result.winner == 1) {
            candidate_winner = candidate1_first ? 1 : 2;
        } else if (result.winner == 2) {
            candidate_winner = candidate1_first ? 2 : 1;
        }
        if (candidate_winner == 1) {
            ++summary.candidate1_wins;
        } else if (candidate_winner == 2) {
            ++summary.candidate2_wins;
        } else {
            ++summary.draws;
        }
        summary.games.push_back(MatchGameResult{candidate1_first, candidate_winner, result.moves});
    };

    for (int game_index = 0; game_index < games_per_side; ++game_index) {
        const GameResult result = play_game(
            n_playout1,
            c_puct1,
            mix_seed(seed, game_index * 4),
            rollout_policy1,
            rollout_limit1,
            n_playout2,
            c_puct2,
            mix_seed(seed, game_index * 4 + 1),
            rollout_policy2,
            rollout_limit2);
        record_game(result, true);
    }

    for (int game_index = 0; game_index < games_per_side; ++game_index) {
        const GameResult result = play_game(
            n_playout2,
            c_puct2,
            mix_seed(seed, games_per_side * 4 + game_index * 4),
            rollout_policy2,
            rollout_limit2,
            n_playout1,
            c_puct1,
            mix_seed(seed, games_per_side * 4 + game_index * 4 + 1),
            rollout_policy1,
            rollout_limit1);
        record_game(result, false);
    }

    return summary;
}

std::tuple<float, float> compare_mcts(
    int n_playout1,
    float c_puct1,
    int n_playout2,
    float c_puct2,
    int games_per_side,
    unsigned int seed,
    int rollout_policy1,
    int rollout_limit1,
    int rollout_policy2,
    int rollout_limit2) {
    const MatchSummary summary = compare_mcts_summary(
        n_playout1,
        c_puct1,
        n_playout2,
        c_puct2,
        games_per_side,
        seed,
        rollout_policy1,
        rollout_limit1,
        rollout_policy2,
        rollout_limit2);
    const float score1 = static_cast<float>(summary.candidate1_wins) + 0.5f * summary.draws;
    const float score2 = static_cast<float>(summary.candidate2_wins) + 0.5f * summary.draws;
    return std::make_tuple(score1, score2);
}

py::dict compare_mcts_detailed(
    int n_playout1,
    float c_puct1,
    int n_playout2,
    float c_puct2,
    int games_per_side,
    unsigned int seed,
    int rollout_policy1,
    int rollout_limit1,
    int rollout_policy2,
    int rollout_limit2) {
    const MatchSummary summary = compare_mcts_summary(
        n_playout1,
        c_puct1,
        n_playout2,
        c_puct2,
        games_per_side,
        seed,
        rollout_policy1,
        rollout_limit1,
        rollout_policy2,
        rollout_limit2);
    const float score1 = static_cast<float>(summary.candidate1_wins) + 0.5f * summary.draws;
    const float score2 = static_cast<float>(summary.candidate2_wins) + 0.5f * summary.draws;

    py::list games;
    for (const MatchGameResult& game : summary.games) {
        py::dict item;
        item["candidate1_first"] = game.candidate1_first;
        item["candidate_winner"] = game.candidate_winner;
        item["moves"] = game.moves;
        games.append(item);
    }

    py::dict result;
    result["candidate1_wins"] = summary.candidate1_wins;
    result["candidate2_wins"] = summary.candidate2_wins;
    result["draws"] = summary.draws;
    result["games"] = games;
    result["score1"] = score1;
    result["score2"] = score2;
    result["total_games"] = games_per_side * 2;
    return result;
}

} // namespace

class PyMCTSPure {
public:
    PyMCTSPure(
        int n_playout,
        float c_puct,
        unsigned int seed,
        bool capture_search_tree,
        int rollout_policy,
        int rollout_limit)
        : n_playout(n_playout), c_puct(c_puct), seed_rng(seed),
          rollout_policy(rollout_policy), rollout_limit(rollout_limit),
          engine(n_playout, c_puct, seed_rng, capture_search_tree, rollout_policy, rollout_limit) {
    }

    int get_move(const UltimateTicTacToe& game) {
        UltimateTicTacToe state = game;
        return engine.get_move(state);
    }

    int get_move_with_priors(
        const UltimateTicTacToe& game,
        const std::vector<float>& action_priors,
        const std::vector<int>& allowed_actions,
        bool select_by_q) {
        UltimateTicTacToe state = game;
        return engine.get_move_with_priors(state, action_priors, allowed_actions, select_by_q);
    }

    int suggest_move(const UltimateTicTacToe& game) {
        std::mt19937 temp_rng(seed_rng());
        MCTSPure<UltimateTicTacToe, ActionList> temp_engine(
            n_playout,
            c_puct,
            temp_rng,
            false,
            rollout_policy,
            rollout_limit);
        UltimateTicTacToe state = game;
        return temp_engine.get_move(state);
    }

    void update_with_move(int action) {
        engine.update_with_move(action);
    }

    void reset() {
        engine.update_with_move(-1);
    }

    SearchTreeSnapshot get_last_search_tree() const {
        return engine.get_last_search_tree();
    }

private:
    int n_playout;
    float c_puct;
    std::mt19937 seed_rng;
    int rollout_policy;
    int rollout_limit;
    MCTSPure<UltimateTicTacToe, ActionList> engine;
};

PYBIND11_MODULE(gameai_native, m) {
    m.doc() = "Python bindings for GameAI pure MCTS";
    m.attr("BOARD_SIZE") = BOARD_SIZE;
    m.attr("META_BOARD_SIZE") = META_BOARD_SIZE;
    m.def(
        "compare_mcts",
        &compare_mcts,
        py::arg("n_playout1"),
        py::arg("c_puct1"),
        py::arg("n_playout2"),
        py::arg("c_puct2"),
        py::arg("games_per_side") = 8,
        py::arg("seed") = std::random_device{}(),
        py::arg("rollout_policy1") = 0,
        py::arg("rollout_limit1") = 300,
        py::arg("rollout_policy2") = 0,
        py::arg("rollout_limit2") = 300);
    m.def(
        "compare_mcts_detailed",
        &compare_mcts_detailed,
        py::arg("n_playout1"),
        py::arg("c_puct1"),
        py::arg("n_playout2"),
        py::arg("c_puct2"),
        py::arg("games_per_side") = 8,
        py::arg("seed") = std::random_device{}(),
        py::arg("rollout_policy1") = 0,
        py::arg("rollout_limit1") = 300,
        py::arg("rollout_policy2") = 0,
        py::arg("rollout_limit2") = 300);

    py::class_<UltimateTicTacToe>(m, "UltimateTicTacToe")
        .def(py::init<>())
        .def_static("from_board", [](const std::vector<std::vector<int>>& board,
                                     std::pair<int, int> next_board) {
            if (board.size() != BOARD_SIZE) {
                throw std::runtime_error("board must have 9 rows");
            }
            const bool any_next_board = next_board.first != -1 || next_board.second != -1;
            if (any_next_board &&
                (next_board.first < 0 || next_board.first >= META_BOARD_SIZE ||
                 next_board.second < 0 || next_board.second >= META_BOARD_SIZE)) {
                throw std::runtime_error("next_board must be (-1, -1) or a 3x3 board coordinate");
            }

            NewGameParameters params{};
            params.next_board = next_board;
            for (int row = 0; row < BOARD_SIZE; ++row) {
                if (board[row].size() != BOARD_SIZE) {
                    throw std::runtime_error("board must have 9 columns");
                }
                for (int col = 0; col < BOARD_SIZE; ++col) {
                    if (board[row][col] < 0 || board[row][col] > 2) {
                        throw std::runtime_error("board values must be 0, 1, or 2");
                    }
                    params.board[row][col] = board[row][col];
                }
            }
            return UltimateTicTacToe(params);
        })
        .def("clone", [](const UltimateTicTacToe& game) {
            return UltimateTicTacToe(*game.clone());
        })
        .def("get_board", [](const UltimateTicTacToe& game) {
            return board_to_vector(game.get_board());
        })
        .def("get_meta_board", [](const UltimateTicTacToe& game) {
            return meta_board_to_vector(game.get_meta_board());
        })
        .def("get_next_board", &UltimateTicTacToe::get_next_board)
        .def("get_valid_actions", [](const UltimateTicTacToe& game) {
            auto actions = game.get_valid_actions();
            std::vector<int> result;
            result.reserve(actions.size());
            for (int index = 0; index < actions.size(); ++index) {
                result.push_back(actions[index]);
            }
            return result;
        })
        .def("is_action_valid", &UltimateTicTacToe::is_action_valid)
        .def("make_move", &UltimateTicTacToe::make_move)
        .def("make_move_rc", [](UltimateTicTacToe& game, int row, int col) {
            return game.make_move(game.get_action_index(row, col));
        })
        .def("undo_move_rc", [](UltimateTicTacToe& game, int row, int col) {
            game.undo_move(std::make_pair(row, col));
        })
        .def("get_done_winner", &UltimateTicTacToe::get_done_winner)
        .def("get_current_player", &UltimateTicTacToe::get_current_player)
        .def("get_action_index", &UltimateTicTacToe::get_action_index)
        .def("get_row_col", &UltimateTicTacToe::get_row_col)
        .def("get_step", &UltimateTicTacToe::get_step);

    py::class_<PyMCTSPure>(m, "MCTSPure")
        .def(py::init<int, float, unsigned int, bool, int, int>(),
             py::arg("n_playout"),
             py::arg("c_puct"),
             py::arg("seed") = std::random_device{}(),
             py::arg("capture_search_tree") = false,
             py::arg("rollout_policy") = 1,
             py::arg("rollout_limit") = 32)
        .def("get_move", &PyMCTSPure::get_move)
        .def(
            "get_move_with_priors",
            &PyMCTSPure::get_move_with_priors,
            py::arg("game"),
            py::arg("action_priors"),
            py::arg("allowed_actions") = std::vector<int>{},
            py::arg("select_by_q") = false)
        .def("suggest_move", &PyMCTSPure::suggest_move)
        .def("update_with_move", &PyMCTSPure::update_with_move)
        .def("reset", &PyMCTSPure::reset)
        .def("get_last_search_tree", [](const PyMCTSPure& engine) {
            return search_tree_snapshot_to_dict(engine.get_last_search_tree());
        });
}
