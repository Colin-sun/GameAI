const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function makeWorkerContext() {
  const messages = [];
  const context = {
    console,
    DataView,
    Float32Array,
    Uint8Array,
    TextDecoder,
    Math,
    Number,
    Set,
    Map,
    Promise,
    Error,
    TypeError,
    Infinity,
    NaN,
    isFinite,
    setTimeout,
    clearTimeout,
    fetch: async (url) => {
      const bytes = fs.readFileSync(new URL(url).pathname);
      const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      return { ok: true, status: 200, arrayBuffer: async () => buffer };
    },
  };
  context.self = context;
  context.postMessage = (message) => messages.push(message);
  context.importScripts = (...files) => {
    for (const file of files) {
      const source = fs.readFileSync(path.join(__dirname, "..", file), "utf8");
      vm.runInContext(source, context, { filename: file });
    }
  };
  vm.createContext(context);
  const workerSource = fs.readFileSync(path.join(__dirname, "..", "ai-worker.js"), "utf8");
  vm.runInContext(workerSource, context, { filename: "ai-worker.js" });
  return { context, messages };
}

function waitForMessage(messages, predicate) {
  return new Promise((resolve, reject) => {
    const started = Date.now();
    const check = () => {
      const found = messages.find(predicate);
      if (found) {
        resolve(found);
        return;
      }
      if (Date.now() - started > 10000) {
        reject(new Error(`worker messages: ${JSON.stringify(messages)}`));
        return;
      }
      setTimeout(check, 10);
    };
    check();
  });
}

test("AI worker runs a pure search and reports progress/result", async () => {
  const { context, messages } = makeWorkerContext();
  context.self.onmessage({ data: {
    type: "search",
    id: 1,
    moves: "",
    mode: "pure",
    playouts: 4,
    cPuct: 0.8,
    rolloutLimit: 4,
    seed: 3,
    chunkSize: 1,
  } });
  const result = await waitForMessage(messages, (message) => message.type === "result" && message.id === 1);
  assert.ok(result.result.siteAction >= 0 && result.result.siteAction < 81);
  assert.ok(messages.some((message) => message.type === "progress" && message.id === 1));
});

test("AI worker loads and runs the prior model", async () => {
  const { context, messages } = makeWorkerContext();
  const file = path.join(__dirname, "..", "models", "utt_majority_v1_torch_teacher6000_512_hard.bin");
  context.self.onmessage({ data: {
    type: "search",
    id: 2,
    moves: "",
    mode: "prior",
    playouts: 1,
    cPuct: 0.8,
    rolloutLimit: 2,
    seed: 4,
    chunkSize: 1,
    modelUrl: `file://${file}`,
  } });
  const result = await waitForMessage(messages, (message) => message.type === "result" && message.id === 2);
  assert.equal(result.result.metadata.ruleVersion, "majority-utt-v1");
  assert.ok(result.result.siteAction >= 0 && result.result.siteAction < 81);
});

