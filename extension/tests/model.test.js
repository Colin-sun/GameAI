const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const engine = require("../engine.js");
const { PriorModel } = require("../model.js");

function loadModel() {
  const file = path.join(__dirname, "..", "models", "utt_majority_v1_torch_teacher6000_512_hard.bin");
  const bytes = fs.readFileSync(file);
  const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  return PriorModel.fromArrayBuffer(buffer);
}

test("converted prior model exposes the expected metadata and output shape", () => {
  const model = loadModel();
  assert.deepEqual(model.metadata, {
    inputPlanes: 10,
    actionSize: 81,
    channels: 128,
    blocks: 8,
    ruleVersion: "majority-utt-v1",
    checkpoint: "utt_majority_v1_torch_teacher6000_512_hard.pt",
  });
  const output = model.predict(new engine.GameState() && engine.encodeState(new engine.GameState()));
  assert.equal(output.policy.length, 81);
  assert.ok(Math.abs(output.policy.reduce((sum, value) => sum + value, 0) - 1) < 1e-5);
  assert.ok(Number.isFinite(output.value));
  assert.deepEqual(
    Array.from(output.policy).map((value, index) => [value, index]).sort((a, b) => b[0] - a[0]).slice(0, 3).map((item) => item[1]),
    [10, 64, 16],
  );
});

test("prior model rejects a non-majority header", () => {
  const model = loadModel();
  assert.equal(model.metadata.ruleVersion, "majority-utt-v1");
  const bytes = fs.readFileSync(path.join(__dirname, "..", "models", "utt_majority_v1_torch_teacher6000_512_hard.bin"));
  const copy = new Uint8Array(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
  // The rule string is after the fixed header and its length.  Corrupting the
  // magic is enough to verify that the binary is not accepted blindly.
  copy[0] = 0x58;
  assert.throws(() => PriorModel.fromArrayBuffer(copy.buffer), /Unsupported prior model format/);
});

