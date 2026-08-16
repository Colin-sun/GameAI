#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

emcc extension/wasm/engine.c \
  -O3 -flto -ffast-math -msimd128 \
  -s STANDALONE_WASM=1 \
  -s EXPORTED_FUNCTIONS='["_gameai_model_ptr","_gameai_model_capacity","_gameai_model_length","_gameai_input_board_ptr","_gameai_input_meta_ptr","_gameai_set_model_loaded","_gameai_init","_gameai_run","_gameai_completed","_gameai_requested","_gameai_status","_gameai_action","_gameai_root_visits","_gameai_node_count","_gameai_action_q","_gameai_infer_ptr","_gameai_policy_ptr","_gameai_value_ptr","_gameai_infer"]' \
  -s EXPORTED_RUNTIME_METHODS='[]' \
  -s INITIAL_MEMORY=67108864 \
  -s ALLOW_MEMORY_GROWTH=0 \
  -s ERROR_ON_UNDEFINED_SYMBOLS=1 \
  -o extension/wasm/engine.wasm

