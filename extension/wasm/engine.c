#include <math.h>
#include <stdint.h>
#include <string.h>

/*
 * Standalone WebAssembly backend for the browser extension.
 *
 * The public interface is intentionally small. JavaScript copies the current
 * position and (for native-prior) the packed Torch weights into the exported
 * memory, starts a search, and advances it in bounded batches. All hot loops
 * stay inside this module so the extension does not allocate JavaScript
 * objects for every rollout.
 */

#define BOARD_SIZE 9
#define CELLS 81
#define META_CELLS 9
#define INPUT_FLOATS (10 * CELLS)

#define MAX_NODES 262144
#define MAX_EDGES 524288
#define MODEL_FLOATS 3000000

#define FEATURE_CHANNELS 128
#define FEATURE_FLOATS (FEATURE_CHANNELS * CELLS)
#define HEAD_CHANNELS 32
#define HEAD_FLOATS (HEAD_CHANNELS * CELLS)

#define STATUS_OK 0
#define STATUS_DONE 1
#define STATUS_CAPACITY 2
#define STATUS_MODEL 3

#define EXPORT __attribute__((used)) __attribute__((visibility("default")))

typedef struct {
    uint8_t board[CELLS];
    uint8_t meta[META_CELLS];
    uint16_t local_masks[2][META_CELLS];
    int8_t next_row;
    int8_t next_col;
    uint8_t current_player;
    uint8_t step;
} State;

typedef struct {
    int first_edge;
    int edge_count;
    int visits;
    float value_sum;
} SearchNode;

typedef struct {
    int action;
    int child;
    float prior;
} SearchEdge;

static const int LOCAL_LINES[8][3] = {
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8},
    {0, 3, 6},
    {1, 4, 7},
    {2, 5, 8},
    {0, 4, 8},
    {2, 4, 6},
};

static const uint16_t LOCAL_LINE_MASKS[8] = {
    0x007,
    0x038,
    0x1c0,
    0x049,
    0x092,
    0x124,
    0x111,
    0x054,
};

/* The packed model order matches extension/wasm-runtime.js. */
#define CONV_WEIGHT_FLOATS (128 * 128 * 9)
#define STEM_WEIGHT_FLOATS (128 * 10 * 9)
#define STEM_WEIGHT_OFFSET 0
#define STEM_BIAS_OFFSET (STEM_WEIGHT_OFFSET + STEM_WEIGHT_FLOATS)
#define RESIDUAL_OFFSET (STEM_BIAS_OFFSET + 128)
#define RESIDUAL_BLOCK_FLOATS (CONV_WEIGHT_FLOATS + 128 + CONV_WEIGHT_FLOATS + 128)
#define POLICY_CONV_WEIGHT_OFFSET (RESIDUAL_OFFSET + 8 * RESIDUAL_BLOCK_FLOATS)
#define POLICY_CONV_BIAS_OFFSET (POLICY_CONV_WEIGHT_OFFSET + (32 * 128))
#define POLICY_FC_WEIGHT_OFFSET (POLICY_CONV_BIAS_OFFSET + 32)
#define POLICY_FC_BIAS_OFFSET (POLICY_FC_WEIGHT_OFFSET + (81 * HEAD_FLOATS))
#define VALUE_CONV_WEIGHT_OFFSET (POLICY_FC_BIAS_OFFSET + 81)
#define VALUE_CONV_BIAS_OFFSET (VALUE_CONV_WEIGHT_OFFSET + (32 * 128))
#define VALUE_FC1_WEIGHT_OFFSET (VALUE_CONV_BIAS_OFFSET + 32)
#define VALUE_FC1_BIAS_OFFSET (VALUE_FC1_WEIGHT_OFFSET + (128 * HEAD_FLOATS))
#define VALUE_FC2_WEIGHT_OFFSET (VALUE_FC1_BIAS_OFFSET + 128)
#define VALUE_FC2_BIAS_OFFSET (VALUE_FC2_WEIGHT_OFFSET + 128)
#define MODEL_USED_FLOATS (VALUE_FC2_BIAS_OFFSET + 1)

static uint8_t input_board[CELLS];
static uint8_t input_meta[META_CELLS];
static float model_weights[MODEL_FLOATS];

static SearchNode nodes[MAX_NODES];
static SearchEdge edges[MAX_EDGES];
static State root_state;
static int node_count;
static int edge_count;
static int completed_playouts;
static int requested_playouts;
static int search_mode;
static int search_rollout_limit;
static int search_root_q;
static float search_c_puct;
static float search_policy_exponent;
static uint32_t random_state;
static int search_status;
static int model_loaded;

static float model_input[INPUT_FLOATS];
static float feature_a[FEATURE_FLOATS];
static float feature_b[FEATURE_FLOATS];
static float feature_residual[FEATURE_FLOATS];
static float head_a[HEAD_FLOATS];
static float head_b[HEAD_FLOATS];
static float value_hidden[128];
static float policy_logits[CELLS];
static float policy_output[CELLS];
static float value_output;

static int local_base(int meta_row, int meta_col) {
    return (meta_row * 3) * BOARD_SIZE + meta_col * 3;
}

static int local_win_with(
    const State* state,
    int meta_row,
    int meta_col,
    int action,
    int player) {
    const int meta_index = meta_row * 3 + meta_col;
    const int local_index = ((action / BOARD_SIZE) % 3) * 3 + action % 3;
    uint16_t mask = state->local_masks[player - 1][meta_index];
    mask = (uint16_t)(mask | (uint16_t)(1u << local_index));
    for (int line_index = 0; line_index < 8; ++line_index) {
        if ((mask & LOCAL_LINE_MASKS[line_index]) == LOCAL_LINE_MASKS[line_index]) return 1;
    }
    return 0;
}

static int local_state_after(const State* state, int action, int player) {
    const int row = action / BOARD_SIZE;
    const int col = action % BOARD_SIZE;
    const int meta_row = row / 3;
    const int meta_col = col / 3;
    if (local_win_with(state, meta_row, meta_col, action, player)) return player;
    const int meta_index = meta_row * 3 + meta_col;
    const uint16_t occupied = (uint16_t)(
        state->local_masks[0][meta_index] | state->local_masks[1][meta_index]);
    return occupied == 0x1ff ? 3 : 0;
}

static int majority_winner(const uint8_t* meta) {
    int player_one_wins = 0;
    int player_two_wins = 0;
    int draws = 0;
    int resolved = 0;
    for (int index = 0; index < META_CELLS; ++index) {
        player_one_wins += meta[index] == 1;
        player_two_wins += meta[index] == 2;
        draws += meta[index] == 3;
        resolved += meta[index] != 0;
    }
    const int threshold = (9 - draws) / 2 + 1;
    if (player_one_wins >= threshold) return 1;
    if (player_two_wins >= threshold) return 2;
    if (resolved == 9) {
        if (player_one_wins > player_two_wins) return 1;
        if (player_two_wins > player_one_wins) return 2;
        return -1;
    }
    return 0;
}

static int action_valid(const State* state, int action) {
    if (action < 0 || action >= CELLS || state->board[action] != 0) return 0;
    const int row = action / BOARD_SIZE;
    const int col = action % BOARD_SIZE;
    const int meta_row = row / 3;
    const int meta_col = col / 3;
    if (state->meta[meta_row * 3 + meta_col] != 0) return 0;
    if (state->next_row >= 0 && state->next_col >= 0) {
        if (meta_row != state->next_row || meta_col != state->next_col) return 0;
    }
    return 1;
}

static int get_actions(const State* state, int* actions) {
    if (majority_winner(state->meta) != 0) return 0;
    int count = 0;
    if (state->next_row >= 0 && state->next_col >= 0 &&
        state->meta[state->next_row * 3 + state->next_col] == 0) {
        const int base = local_base(state->next_row, state->next_col);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const int action = base + row * BOARD_SIZE + col;
                if (state->board[action] == 0) actions[count++] = action;
            }
        }
        return count;
    }

    for (int action = 0; action < CELLS; ++action) {
        if (state->board[action] == 0) {
            const int meta_index = (action / 27) * 3 + ((action % 9) / 3);
            if (state->meta[meta_index] == 0) actions[count++] = action;
        }
    }
    return count;
}

static int make_move(State* state, int action) {
    if (!action_valid(state, action)) return 0;
    const int player = state->current_player;
    const int row = action / BOARD_SIZE;
    const int col = action % BOARD_SIZE;
    const int meta_row = row / 3;
    const int meta_col = col / 3;
    state->board[action] = (uint8_t)player;
    const int local_index = (row % 3) * 3 + col % 3;
    state->local_masks[player - 1][meta_row * 3 + meta_col] = (uint16_t)(
        state->local_masks[player - 1][meta_row * 3 + meta_col] | (uint16_t)(1u << local_index));
    state->meta[meta_row * 3 + meta_col] = (uint8_t)local_state_after(state, action, player);

    const int target_row = row % 3;
    const int target_col = col % 3;
    if (state->meta[target_row * 3 + target_col] == 0) {
        state->next_row = (int8_t)target_row;
        state->next_col = (int8_t)target_col;
    } else {
        state->next_row = -1;
        state->next_col = -1;
    }
    state->current_player = (uint8_t)(3 - player);
    state->step = (uint8_t)(state->step + 1);
    return 1;
}

static int completes_local(const State* state, int action, int player) {
    if (!action_valid(state, action) || (player != 1 && player != 2)) return 0;
    const int row = action / BOARD_SIZE;
    const int col = action % BOARD_SIZE;
    return local_win_with(state, row / 3, col / 3, action, player);
}

static int wins_game_with(const State* state, int action, int player) {
    if (!completes_local(state, action, player)) return 0;
    const int row = action / BOARD_SIZE;
    const int col = action % BOARD_SIZE;
    const int meta_index = (row / 3) * 3 + col / 3;
    int player_one_wins = 0;
    int player_two_wins = 0;
    int draws = 0;
    int resolved = 0;
    for (int index = 0; index < META_CELLS; ++index) {
        int value = state->meta[index];
        if (index == meta_index) value = player;
        player_one_wins += value == 1;
        player_two_wins += value == 2;
        draws += value == 3;
        resolved += value != 0;
    }
    const int threshold = (9 - draws) / 2 + 1;
    if (player == 1) return player_one_wins >= threshold;
    if (player == 2) return player_two_wins >= threshold;
    (void)resolved;
    return 0;
}

static uint32_t random_u32(void) {
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static int random_index(int length) {
    return (int)(((uint64_t)random_u32() * (uint64_t)length) >> 32);
}

static int select_rollout_action(
    const State* state,
    const int* actions,
    int action_count,
    int tactical) {
    if (!tactical) return actions[random_index(action_count)];

    const int player = state->current_player;
    const int opponent = 3 - player;
    int candidates[CELLS];
    int candidate_count = 0;
    int best_priority = -1;
    for (int index = 0; index < action_count; ++index) {
        const int action = actions[index];
        int priority = 0;
        if (wins_game_with(state, action, player)) {
            priority = 4;
        } else if (wins_game_with(state, action, opponent)) {
            priority = 3;
        } else if (completes_local(state, action, player)) {
            priority = 2;
        } else if (completes_local(state, action, opponent)) {
            priority = 1;
        }
        if (priority > best_priority) {
            best_priority = priority;
            candidate_count = 0;
        }
        if (priority == best_priority) candidates[candidate_count++] = action;
    }
    return candidates[random_index(candidate_count)];
}

static float evaluate_state(const State* state, int player) {
    if (player != 1 && player != 2) return 0.0f;
    const int result = majority_winner(state->meta);
    if (result != 0) {
        if (result == -1) return 0.0f;
        return result == player ? 1.0f : -1.0f;
    }

    const int opponent = 3 - player;
    int own_meta = 0;
    int opponent_meta = 0;
    for (int index = 0; index < META_CELLS; ++index) {
        own_meta += state->meta[index] == player;
        opponent_meta += state->meta[index] == opponent;
    }
    float score = 0.35f * (float)(own_meta - opponent_meta);
    for (int meta_row = 0; meta_row < 3; ++meta_row) {
        for (int meta_col = 0; meta_col < 3; ++meta_col) {
            if (state->meta[meta_row * 3 + meta_col] != 0) continue;
            const int base = local_base(meta_row, meta_col);
            for (int line_index = 0; line_index < 8; ++line_index) {
                const int* line = LOCAL_LINES[line_index];
                int own = 0;
                int theirs = 0;
                for (int member = 0; member < 3; ++member) {
                    const int cell = line[member];
                    const int value = state->board[
                        base + (cell / 3) * BOARD_SIZE + cell % 3];
                    own += value == player;
                    theirs += value == opponent;
                }
                if (theirs == 0 && own > 0) {
                    score += own == 2 ? 0.055f : 0.012f;
                } else if (own == 0 && theirs > 0) {
                    score -= theirs == 2 ? 0.055f : 0.012f;
                }
            }
        }
    }
    return tanhf(score);
}

static float rollout_value(State* state) {
    const int perspective = state->current_player;
    const int tactical = search_mode != 0;
    int actions[CELLS];
    for (int index = 0; index < search_rollout_limit; ++index) {
        const int result = majority_winner(state->meta);
        if (result != 0) {
            if (result == -1) return 0.0f;
            return result == perspective ? 1.0f : -1.0f;
        }
        const int action_count = get_actions(state, actions);
        if (action_count <= 0) return 0.0f;
        const int action = select_rollout_action(state, actions, action_count, tactical);
        if (!make_move(state, action)) return 0.0f;
    }

    const int result = majority_winner(state->meta);
    if (result != 0) {
        if (result == -1) return 0.0f;
        return result == perspective ? 1.0f : -1.0f;
    }
    return evaluate_state(state, perspective);
}

static void initialize_node(int index) {
    nodes[index].first_edge = -1;
    nodes[index].edge_count = 0;
    nodes[index].visits = 0;
    nodes[index].value_sum = 0.0f;
}

static int expand_node(int node_index, const int* actions, int action_count, const float* priors) {
    if (action_count <= 0) return 1;
    if (node_count + action_count > MAX_NODES || edge_count + action_count > MAX_EDGES) {
        search_status = STATUS_CAPACITY;
        return 0;
    }
    const int first_edge = edge_count;
    for (int index = 0; index < action_count; ++index) {
        const int child = node_count++;
        initialize_node(child);
        edges[edge_count].action = actions[index];
        edges[edge_count].child = child;
        edges[edge_count].prior = priors ? priors[index] : 1.0f / (float)action_count;
        edge_count += 1;
    }
    nodes[node_index].first_edge = first_edge;
    nodes[node_index].edge_count = action_count;
    return 1;
}

static int select_edge(int node_index) {
    const SearchNode* node = &nodes[node_index];
    const float parent_visits = sqrtf((float)node->visits);
    int best_edge = -1;
    int best_action = CELLS + 1;
    float best_score = -INFINITY;
    for (int offset = 0; offset < node->edge_count; ++offset) {
        const int edge_index = node->first_edge + offset;
        const SearchEdge* edge = &edges[edge_index];
        const SearchNode* child = &nodes[edge->child];
        const float q = child->visits ? child->value_sum / (float)child->visits : 0.0f;
        const float score = q + search_c_puct * edge->prior * parent_visits /
            (1.0f + (float)child->visits);
        if (score > best_score || (score == best_score && edge->action < best_action)) {
            best_score = score;
            best_action = edge->action;
            best_edge = edge_index;
        }
    }
    return best_edge;
}

static void run_playout(void) {
    State state = root_state;
    int path[128];
    int path_length = 1;
    int node_index = 0;

    while (nodes[node_index].edge_count > 0) {
        const int edge_index = select_edge(node_index);
        if (edge_index < 0) {
            search_status = STATUS_CAPACITY;
            return;
        }
        const SearchEdge* edge = &edges[edge_index];
        if (!make_move(&state, edge->action)) {
            search_status = STATUS_CAPACITY;
            return;
        }
        node_index = edge->child;
        if (path_length < (int)(sizeof(path) / sizeof(path[0]))) {
            path[path_length++] = node_index;
        }
    }

    int actions[CELLS];
    if (majority_winner(state.meta) == 0) {
        const int action_count = get_actions(&state, actions);
        float priors[CELLS];
        for (int index = 0; index < action_count; ++index) {
            priors[index] = 1.0f / (float)action_count;
        }
        if (!expand_node(node_index, actions, action_count, priors)) return;
    }

    float value = -rollout_value(&state);
    for (int index = path_length - 1; index >= 0; --index) {
        SearchNode* node = &nodes[path[index]];
        node->visits += 1;
        node->value_sum += value;
        value = -value;
    }
}

static void conv3(
    const float* input,
    const float* weights,
    const float* bias,
    int input_channels,
    int output_channels,
    float* output) {
    for (int output_channel = 0; output_channel < output_channels; ++output_channel) {
        const int output_offset = output_channel * CELLS;
        const int weight_channel_offset = output_channel * input_channels * 9;
        for (int row = 0; row < BOARD_SIZE; ++row) {
            for (int col = 0; col < BOARD_SIZE; ++col) {
                float sum = bias[output_channel];
                for (int input_channel = 0; input_channel < input_channels; ++input_channel) {
                    const int input_offset = input_channel * CELLS;
                    const int weight_offset = weight_channel_offset + input_channel * 9;
                    for (int kernel_row = 0; kernel_row < 3; ++kernel_row) {
                        const int source_row = row + kernel_row - 1;
                        if (source_row < 0 || source_row >= BOARD_SIZE) continue;
                        for (int kernel_col = 0; kernel_col < 3; ++kernel_col) {
                            const int source_col = col + kernel_col - 1;
                            if (source_col < 0 || source_col >= BOARD_SIZE) continue;
                            sum += input[input_offset + source_row * BOARD_SIZE + source_col] *
                                weights[weight_offset + kernel_row * 3 + kernel_col];
                        }
                    }
                }
                output[output_offset + row * BOARD_SIZE + col] = sum;
            }
        }
    }
}

static void conv1(
    const float* input,
    const float* weights,
    const float* bias,
    int input_channels,
    int output_channels,
    float* output) {
    for (int output_channel = 0; output_channel < output_channels; ++output_channel) {
        const int output_offset = output_channel * CELLS;
        const int weight_offset = output_channel * input_channels;
        for (int cell = 0; cell < CELLS; ++cell) {
            float sum = bias[output_channel];
            for (int input_channel = 0; input_channel < input_channels; ++input_channel) {
                sum += input[input_channel * CELLS + cell] *
                    weights[weight_offset + input_channel];
            }
            output[output_offset + cell] = sum;
        }
    }
}

static void relu_in_place(float* values, int length) {
    for (int index = 0; index < length; ++index) {
        if (values[index] < 0.0f) values[index] = 0.0f;
    }
}

static void dense(
    const float* input,
    const float* weights,
    const float* bias,
    int input_size,
    int output_size,
    float* output) {
    for (int output_index = 0; output_index < output_size; ++output_index) {
        float sum = bias[output_index];
        const int weight_offset = output_index * input_size;
        for (int input_index = 0; input_index < input_size; ++input_index) {
            sum += input[input_index] * weights[weight_offset + input_index];
        }
        output[output_index] = sum;
    }
}

static void softmax(const float* logits, float* output, int length) {
    float maximum = -INFINITY;
    for (int index = 0; index < length; ++index) {
        if (logits[index] > maximum) maximum = logits[index];
    }
    float total = 0.0f;
    for (int index = 0; index < length; ++index) {
        output[index] = expf(logits[index] - maximum);
        total += output[index];
    }
    if (!(total > 0.0f) || !isfinite(total)) {
        for (int index = 0; index < length; ++index) output[index] = 1.0f / (float)length;
        return;
    }
    for (int index = 0; index < length; ++index) output[index] /= total;
}

static void infer_encoded(const float* encoded, float* policy, float* value) {
    const float* weights = model_weights;
    conv3(
        encoded,
        weights + STEM_WEIGHT_OFFSET,
        weights + STEM_BIAS_OFFSET,
        10,
        FEATURE_CHANNELS,
        feature_a);
    relu_in_place(feature_a, FEATURE_FLOATS);

    float* features = feature_a;
    float* scratch = feature_b;
    for (int block = 0; block < 8; ++block) {
        const int base = RESIDUAL_OFFSET + block * RESIDUAL_BLOCK_FLOATS;
        memcpy(feature_residual, features, sizeof(feature_residual));
        conv3(
            features,
            weights + base,
            weights + base + CONV_WEIGHT_FLOATS,
            FEATURE_CHANNELS,
            FEATURE_CHANNELS,
            scratch);
        relu_in_place(scratch, FEATURE_FLOATS);
        conv3(
            scratch,
            weights + base + CONV_WEIGHT_FLOATS + 128,
            weights + base + CONV_WEIGHT_FLOATS + 128 + CONV_WEIGHT_FLOATS,
            FEATURE_CHANNELS,
            FEATURE_CHANNELS,
            features);
        for (int index = 0; index < FEATURE_FLOATS; ++index) {
            features[index] = features[index] + feature_residual[index];
            if (features[index] < 0.0f) features[index] = 0.0f;
        }
    }

    conv1(
        features,
        weights + POLICY_CONV_WEIGHT_OFFSET,
        weights + POLICY_CONV_BIAS_OFFSET,
        FEATURE_CHANNELS,
        HEAD_CHANNELS,
        head_a);
    relu_in_place(head_a, HEAD_FLOATS);
    dense(
        head_a,
        weights + POLICY_FC_WEIGHT_OFFSET,
        weights + POLICY_FC_BIAS_OFFSET,
        HEAD_FLOATS,
        CELLS,
        policy_logits);
    softmax(policy_logits, policy, CELLS);

    conv1(
        features,
        weights + VALUE_CONV_WEIGHT_OFFSET,
        weights + VALUE_CONV_BIAS_OFFSET,
        FEATURE_CHANNELS,
        HEAD_CHANNELS,
        head_b);
    relu_in_place(head_b, HEAD_FLOATS);
    dense(
        head_b,
        weights + VALUE_FC1_WEIGHT_OFFSET,
        weights + VALUE_FC1_BIAS_OFFSET,
        HEAD_FLOATS,
        128,
        value_hidden);
    relu_in_place(value_hidden, 128);
    float value_raw[1];
    dense(
        value_hidden,
        weights + VALUE_FC2_WEIGHT_OFFSET,
        weights + VALUE_FC2_BIAS_OFFSET,
        128,
        1,
        value_raw);
    *value = tanhf(value_raw[0]);
}

static void encode_state(const State* state) {
    memset(model_input, 0, sizeof(model_input));
    const int player = state->current_player;
    const int opponent = 3 - player;
    for (int action = 0; action < CELLS; ++action) {
        model_input[action] = state->board[action] == player;
        model_input[CELLS + action] = state->board[action] == opponent;
    }
    const int meta_values[4] = {player, opponent, 3, 0};
    for (int meta_plane = 0; meta_plane < 4; ++meta_plane) {
        const int plane_offset = (2 + meta_plane) * CELLS;
        for (int meta_row = 0; meta_row < 3; ++meta_row) {
            for (int meta_col = 0; meta_col < 3; ++meta_col) {
                const float value = state->meta[meta_row * 3 + meta_col] == meta_values[meta_plane];
                const int base = local_base(meta_row, meta_col);
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        model_input[plane_offset + base + row * BOARD_SIZE + col] = value;
                    }
                }
            }
        }
    }
    const int target_offset = 6 * CELLS;
    const int free_offset = 7 * CELLS;
    if (state->next_row < 0) {
        for (int action = 0; action < CELLS; ++action) model_input[free_offset + action] = 1.0f;
    } else {
        const int base = local_base(state->next_row, state->next_col);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                model_input[target_offset + base + row * BOARD_SIZE + col] = 1.0f;
            }
        }
    }
    const int player_offset = 8 * CELLS;
    const int step_offset = 9 * CELLS;
    for (int action = 0; action < CELLS; ++action) {
        model_input[player_offset + action] = player == 1 ? 1.0f : 0.0f;
        model_input[step_offset + action] = (float)state->step / (float)CELLS;
    }
}

static int prepare_root(void) {
    int actions[CELLS];
    const int action_count = get_actions(&root_state, actions);
    if (action_count <= 0) {
        search_status = STATUS_DONE;
        return 1;
    }

    float priors[CELLS];
    if (search_mode == 2) {
        if (!model_loaded || MODEL_USED_FLOATS > MODEL_FLOATS) {
            search_status = STATUS_MODEL;
            return 0;
        }
        encode_state(&root_state);
        float value = 0.0f;
        infer_encoded(model_input, policy_output, &value);
        (void)value;
        float total = 0.0f;
        for (int index = 0; index < action_count; ++index) {
            const int action = actions[index];
            const float raw = policy_output[action] > 0.0f ? policy_output[action] : 0.0f;
            priors[index] = powf(raw, search_policy_exponent);
            total += priors[index];
        }
        if (!(total > 0.0f) || !isfinite(total)) {
            for (int index = 0; index < action_count; ++index) {
                priors[index] = 1.0f / (float)action_count;
            }
        } else {
            for (int index = 0; index < action_count; ++index) priors[index] /= total;
        }
    } else {
        for (int index = 0; index < action_count; ++index) {
            priors[index] = 1.0f / (float)action_count;
        }
    }
    return expand_node(0, actions, action_count, priors);
}

EXPORT int gameai_model_ptr(void) {
    return (int)(uintptr_t)model_weights;
}

EXPORT int gameai_model_capacity(void) {
    return MODEL_FLOATS;
}

EXPORT int gameai_model_length(void) {
    return MODEL_USED_FLOATS;
}

EXPORT int gameai_input_board_ptr(void) {
    return (int)(uintptr_t)input_board;
}

EXPORT int gameai_input_meta_ptr(void) {
    return (int)(uintptr_t)input_meta;
}

EXPORT int gameai_set_model_loaded(int loaded) {
    model_loaded = loaded ? 1 : 0;
    return model_loaded;
}

EXPORT int gameai_init(
    int next_row,
    int next_col,
    int current_player,
    int step,
    int mode,
    int playouts,
    float c_puct,
    int rollout_limit,
    float policy_exponent,
    uint32_t seed,
    int root_q) {
    memcpy(root_state.board, input_board, sizeof(input_board));
    memcpy(root_state.meta, input_meta, sizeof(input_meta));
    memset(root_state.local_masks, 0, sizeof(root_state.local_masks));
    for (int action = 0; action < CELLS; ++action) {
        const int player = root_state.board[action];
        if (player != 1 && player != 2) continue;
        const int row = action / BOARD_SIZE;
        const int col = action % BOARD_SIZE;
        const int meta_index = (row / 3) * 3 + col / 3;
        const int local_index = (row % 3) * 3 + col % 3;
        root_state.local_masks[player - 1][meta_index] = (uint16_t)(
            root_state.local_masks[player - 1][meta_index] | (uint16_t)(1u << local_index));
    }
    root_state.next_row = (int8_t)next_row;
    root_state.next_col = (int8_t)next_col;
    root_state.current_player = (uint8_t)current_player;
    root_state.step = (uint8_t)step;
    search_mode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
    requested_playouts = playouts > 0 ? playouts : 1;
    search_c_puct = c_puct > 0.0f ? c_puct : 0.3f;
    search_rollout_limit = rollout_limit > 0 ? rollout_limit : 32;
    search_policy_exponent = policy_exponent > 0.0f ? policy_exponent : 0.5f;
    search_root_q = root_q ? 1 : 0;
    random_state = seed ? seed : 0x6d2b79f5u;
    node_count = 1;
    edge_count = 0;
    completed_playouts = 0;
    search_status = STATUS_OK;
    initialize_node(0);
    if (majority_winner(root_state.meta) != 0) {
        search_status = STATUS_DONE;
        return search_status;
    }
    if (!prepare_root()) return search_status;
    return search_status;
}

EXPORT int gameai_run(int batch_size) {
    if (search_status != STATUS_OK) return completed_playouts;
    int count = batch_size > 0 ? batch_size : 1;
    while (count-- > 0 && completed_playouts < requested_playouts) {
        run_playout();
        if (search_status != STATUS_OK) break;
        completed_playouts += 1;
    }
    return completed_playouts;
}

EXPORT int gameai_completed(void) {
    return completed_playouts;
}

EXPORT int gameai_requested(void) {
    return requested_playouts;
}

EXPORT int gameai_status(void) {
    return search_status;
}

EXPORT int gameai_action(void) {
    if (node_count <= 1 || nodes[0].edge_count <= 0) return -1;
    int best_edge = -1;
    int best_action = CELLS + 1;
    int best_visits = -1;
    float best_q = -INFINITY;
    for (int offset = 0; offset < nodes[0].edge_count; ++offset) {
        const SearchEdge* edge = &edges[nodes[0].first_edge + offset];
        const SearchNode* child = &nodes[edge->child];
        const int visits = child->visits;
        const float q = visits ? child->value_sum / (float)visits : 0.0f;
        int better = 0;
        if (search_root_q) {
            if (q > best_q) better = 1;
            else if (q == best_q && visits > best_visits) better = 1;
            else if (q == best_q && visits == best_visits && edge->action < best_action) better = 1;
        } else {
            if (visits > best_visits) better = 1;
            else if (visits == best_visits && edge->action < best_action) better = 1;
        }
        if (better || best_edge < 0) {
            best_edge = nodes[0].first_edge + offset;
            best_action = edge->action;
            best_visits = visits;
            best_q = q;
        }
    }
    return best_edge >= 0 ? best_action : -1;
}

EXPORT int gameai_root_visits(void) {
    return nodes[0].visits;
}

EXPORT int gameai_node_count(void) {
    return node_count;
}

EXPORT float gameai_action_q(void) {
    const int action = gameai_action();
    if (action < 0) return 0.0f;
    for (int offset = 0; offset < nodes[0].edge_count; ++offset) {
        const SearchEdge* edge = &edges[nodes[0].first_edge + offset];
        if (edge->action != action) continue;
        const SearchNode* child = &nodes[edge->child];
        return child->visits ? child->value_sum / (float)child->visits : 0.0f;
    }
    return 0.0f;
}

EXPORT int gameai_infer_ptr(void) {
    return (int)(uintptr_t)model_input;
}

EXPORT int gameai_policy_ptr(void) {
    return (int)(uintptr_t)policy_output;
}

EXPORT int gameai_value_ptr(void) {
    return (int)(uintptr_t)&value_output;
}

EXPORT int gameai_infer(float* encoded, float* policy, float* value) {
    if (!model_loaded) return STATUS_MODEL;
    infer_encoded(encoded, policy, value);
    return STATUS_OK;
}
