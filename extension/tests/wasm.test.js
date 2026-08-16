const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { performance } = require("node:perf_hooks");

const engine = require("../engine.js");
const { PriorModel } = require("../model.js");
const { packedWeights } = require("../wasm-runtime.js");

const WASM_FILE = path.join(__dirname, "..", "wasm", "engine.wasm");
const MODEL_FILE = path.join(__dirname, "..", "models", "utt_majority_v1_torch_teacher6000_512_hard.bin");

function wasiImports() {
  return {
    wasi_snapshot_preview1: {
      args_sizes_get: () => 0,
      args_get: () => 0,
      proc_exit: (code) => { throw new Error(`WASM exited (${code})`); },
    },
  };
}

function loadModel() {
  const bytes = fs.readFileSync(MODEL_FILE);
  return PriorModel.fromArrayBuffer(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
}

async function loadWasm() {
  const bytes = fs.readFileSync(WASM_FILE);
  const { instance } = await WebAssembly.instantiate(bytes, wasiImports());
  return instance.exports;
}

function installEmptyState(wasm) {
  new Uint8Array(wasm.memory.buffer, wasm.gameai_input_board_ptr(), 81).fill(0);
  new Uint8Array(wasm.memory.buffer, wasm.gameai_input_meta_ptr(), 9).fill(0);
}

function installModel(wasm, model) {
  const weights = packedWeights(model);
  assert.equal(weights.length, wasm.gameai_model_length());
  new Float32Array(wasm.memory.buffer, wasm.gameai_model_ptr(), weights.length).set(weights);
  wasm.gameai_set_model_loaded(1);
}

test("WASM inference matches the browser CPU model and exports search state", async () => {
  const wasm = await loadWasm();
  const model = loadModel();
  installModel(wasm, model);
  const state = new engine.GameState();
  const encoded = engine.encodeState(state);
  const inputPtr = wasm.gameai_infer_ptr();
  const policyPtr = wasm.gameai_policy_ptr();
  const valuePtr = wasm.gameai_value_ptr();
  new Float32Array(wasm.memory.buffer, inputPtr, encoded.length).set(encoded);
  assert.equal(wasm.gameai_infer(inputPtr, policyPtr, valuePtr), 0);

  const expected = model.predict(encoded);
  const actual = new Float32Array(wasm.memory.buffer, policyPtr, 81);
  const value = new Float32Array(wasm.memory.buffer, valuePtr, 1)[0];
  let maxPolicyError = 0;
  for (let index = 0; index < actual.length; index += 1) {
    maxPolicyError = Math.max(maxPolicyError, Math.abs(actual[index] - expected.policy[index]));
  }
  assert.ok(maxPolicyError < 1e-4, `policy error ${maxPolicyError}`);
  assert.ok(Math.abs(value - expected.value) < 1e-4, `value error ${value - expected.value}`);
});

test("default tactical WASM search stays below one second on one CPU thread", async () => {
  const wasm = await loadWasm();
  installEmptyState(wasm);
  const started = performance.now();
  assert.equal(wasm.gameai_init(-1, -1, 1, 0, 1, 3000, 0.4, 32, 0.5, 20260815, 0), 0);
  assert.equal(wasm.gameai_run(3000), 3000);
  const elapsed = performance.now() - started;
  assert.equal(wasm.gameai_status(), 0);
  assert.ok(wasm.gameai_action() >= 0 && wasm.gameai_action() < 81);
  assert.ok(elapsed < 1000, `tactical WASM search took ${elapsed.toFixed(1)} ms`);
});

test("WASM implements all requested search presets", async () => {
  const wasm = await loadWasm();
  const model = loadModel();
  installModel(wasm, model);
  const presets = [
    [0, 6200, 0.8, 300, 0.5, 0],
    [1, 3000, 0.4, 32, 0.5, 0],
    [2, 12000, 0.2, 32, 0.5, 1],
  ];
  for (const [mode, playouts, cPuct, rolloutLimit, policyExponent, rootQ] of presets) {
    installEmptyState(wasm);
    assert.equal(
      wasm.gameai_init(-1, -1, 1, 0, mode, playouts, cPuct, rolloutLimit, policyExponent, 20260815, rootQ),
      0,
    );
    assert.equal(wasm.gameai_run(playouts), playouts);
    assert.equal(wasm.gameai_status(), 0);
    assert.ok(wasm.gameai_action() >= 0 && wasm.gameai_action() < 81);
    assert.equal(wasm.gameai_root_visits(), playouts);
  }
});

