const test = require("node:test");
const assert = require("node:assert/strict");

const engine = require("../engine.js");

test("site coordinates round-trip through native coordinates", () => {
  for (let action = 0; action < engine.ACTION_SIZE; action += 1) {
    assert.equal(
      engine.nativeActionToSite(engine.siteActionToNative(action)),
      action,
    );
    assert.match(engine.siteActionLabel(action), /^[a-i][1-9]$/);
  }
});

test("p sequence parsing rejects malformed and repeated moves", () => {
  assert.deepEqual(engine.parseSiteMoves("a1b2"), { ok: true, actions: [0, 10] });
  assert.equal(engine.parseSiteMoves("a1b").ok, false);
  assert.equal(engine.parseSiteMoves("a1a1").ok, false);
  assert.equal(engine.parseSiteMoves("z1").ok, false);
});

test("the first site move maps to the native bottom row and sends the correct board", () => {
  const state = engine.GameState.fromSiteMoves([0]);
  assert.equal(state.board[72], 1);
  assert.equal(state.board[0], 0);
  assert.deepEqual([state.nextBoardRow, state.nextBoardCol], [2, 0]);
  assert.equal(state.currentPlayer, 2);
  assert.equal(state.getValidActions().length, 8);
  assert.ok(state.getValidActions().every((action) => Math.floor(action / 27) === 2));
});

test("majority terminal logic ignores meta-board three-in-a-row", () => {
  const state = new engine.GameState();
  state.metaBoard.set([1, 1, 1, 2, 2, 0, 0, 0, 0]);
  assert.deepEqual(state.getDoneWinner(), { done: false, winner: 0 });
  state.metaBoard.set([1, 1, 1, 1, 1, 0, 0, 0, 0]);
  assert.deepEqual(state.getDoneWinner(), { done: true, winner: 1 });
  state.metaBoard.set([1, 2, 3, 1, 2, 3, 0, 0, 0]);
  assert.deepEqual(state.getDoneWinner(), { done: false, winner: 0 });
  state.metaBoard.set([1, 2, 3, 1, 2, 3, 1, 2, 3]);
  assert.deepEqual(state.getDoneWinner(), { done: true, winner: -1 });
});

test("drawn local boards reduce the strict-majority threshold", () => {
  const state = new engine.GameState();
  state.metaBoard.set([1, 1, 1, 3, 3, 3, 3, 0, 0]);
  assert.deepEqual(state.getDoneWinner(), { done: true, winner: 1 });
  state.metaBoard.set([1, 3, 3, 3, 3, 3, 3, 0, 0]);
  assert.deepEqual(state.getDoneWinner(), { done: false, winner: 0 });
});

test("tactical rollout prioritizes an immediate majority win", () => {
  const state = new engine.GameState();
  state.metaBoard.set([1, 1, 1, 1, 0, 0, 0, 0, 0]);
  state.board[32] = 0;
  state.board[30] = 1;
  state.board[31] = 1;
  state.currentPlayer = 1;
  state.nextBoardRow = -1;
  state.nextBoardCol = -1;
  const action = state.selectRolloutAction(
    state.getValidActions(),
    new engine.RandomSource(7),
    1,
  );
  assert.equal(action, 32);
  assert.equal(state.winsGameWithAction(action, 1), true);
});

test("pure, tactical, and prior searches only return legal actions", async () => {
  const modelPath = require("node:path").join(
    __dirname,
    "..",
    "models",
    "utt_majority_v1_torch_teacher6000_512_hard.bin",
  );
  const fs = require("node:fs");
  const { PriorModel } = require("../model.js");
  const bytes = fs.readFileSync(modelPath);
  const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  const model = PriorModel.fromArrayBuffer(buffer);
  for (const mode of ["pure", "tactical", "prior"]) {
    const state = new engine.GameState();
    const search = new engine.MCTS({ mode, playouts: mode === "prior" ? 2 : 8, seed: 11 });
    const result = await search.search(state, mode === "prior" ? model : null, { chunkSize: 2 });
    assert.ok(state.isActionValid(result.action), `${mode} returned ${result.action}`);
    assert.equal(result.rootVisits, mode === "prior" ? 2 : 8);
    assert.ok(result.nodeCount >= 2);
  }
});

