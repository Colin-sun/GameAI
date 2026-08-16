(function (root) {
  "use strict";

  const MODEL_TENSOR_ORDER = [
    "stem.weight",
    "stem.bias",
  ];
  for (let block = 0; block < 8; block += 1) {
    MODEL_TENSOR_ORDER.push(
      `residual.${block}.conv1.weight`,
      `residual.${block}.conv1.bias`,
      `residual.${block}.conv2.weight`,
      `residual.${block}.conv2.bias`,
    );
  }
  MODEL_TENSOR_ORDER.push(
    "policy_conv.weight",
    "policy_conv.bias",
    "policy_fc.weight",
    "policy_fc.bias",
    "value_conv.weight",
    "value_conv.bias",
    "value_fc1.weight",
    "value_fc1.bias",
    "value_fc2.weight",
    "value_fc2.bias",
  );

  const WASI_IMPORTS = {
    args_sizes_get: () => 0,
    args_get: () => 0,
    proc_exit: (code) => {
      throw new Error(`WASM process exited (${code}).`);
    },
  };

  let modulePromise = null;
  let enginePromise = null;
  let searchTail = Promise.resolve();

  function modeId(mode) {
    if (mode === "native-prior" || mode === "prior") return 2;
    if (mode === "tactical") return 1;
    return 0;
  }

  function packedWeights(model) {
    if (model.wasmPackedWeights) return model.wasmPackedWeights;
    let length = 0;
    for (const name of MODEL_TENSOR_ORDER) {
      const tensor = model.weights[name];
      if (!tensor) throw new Error(`Prior model tensor ${name} is missing.`);
      length += tensor.length;
    }
    const packed = new Float32Array(length);
    let offset = 0;
    for (const name of MODEL_TENSOR_ORDER) {
      const tensor = model.weights[name];
      packed.set(tensor, offset);
      offset += tensor.length;
    }
    model.wasmPackedWeights = packed;
    return packed;
  }

  async function loadModule(url) {
    if (typeof WebAssembly === "undefined") {
      throw new Error("当前浏览器没有 WebAssembly 支持。");
    }
    if (!url) throw new Error("WASM 文件地址不可用。");
    if (!modulePromise || modulePromise.url !== url) {
      const promise = fetch(url).then(async (response) => {
        if (!response.ok) throw new Error(`WASM 文件请求失败 (${response.status})。`);
        const bytes = await response.arrayBuffer();
        return WebAssembly.compile(bytes);
      });
      promise.url = url;
      modulePromise = promise;
    }
    const module = await modulePromise;
    const instance = await WebAssembly.instantiate(module, {
      wasi_snapshot_preview1: WASI_IMPORTS,
    });
    const exports = instance.exports;
    for (const name of [
      "memory",
      "gameai_init",
      "gameai_run",
      "gameai_action",
      "gameai_model_ptr",
      "gameai_input_board_ptr",
      "gameai_input_meta_ptr",
    ]) {
      if (!exports[name]) throw new Error(`WASM 导出 ${name} 不存在。`);
    }
    return { instance, exports };
  }

  async function loadEngine(url) {
    if (!enginePromise || enginePromise.url !== url) {
      const promise = loadModule(url).then((module) => new WasmSearch(module));
      promise.url = url;
      enginePromise = promise;
    }
    return enginePromise;
  }

  function lockSearch(task) {
    const previous = searchTail;
    let release;
    searchTail = new Promise((resolve) => { release = resolve; });
    return previous.then(async () => {
      try {
        return await task();
      } finally {
        release();
      }
    });
  }

  class WasmSearch {
    constructor(module) {
      this.module = module;
      this.exports = module.exports;
      this.memory = this.exports.memory;
      this.modelSignature = null;
    }

    ensureModel(model) {
      if (!model) throw new Error("native-prior 模式没有加载模型。");
      const packed = packedWeights(model);
      const expected = this.exports.gameai_model_length();
      if (packed.length !== expected) {
        throw new Error(`WASM 模型长度不匹配 (${packed.length}/${expected})。`);
      }
      const signature = `${model.metadata.checkpoint}:${packed.length}`;
      if (this.modelSignature === signature) return;
      const capacity = this.exports.gameai_model_capacity();
      if (packed.length > capacity) throw new Error("WASM 模型缓冲区不足。");
      new Float32Array(
        this.memory.buffer,
        this.exports.gameai_model_ptr(),
        packed.length,
      ).set(packed);
      this.exports.gameai_set_model_loaded(1);
      this.modelSignature = signature;
    }

    async search(game, request, model, hooks) {
      return lockSearch(async () => {
        if (hooks.isCancelled && hooks.isCancelled()) throw new Error("SEARCH_CANCELLED");
        const mode = modeId(request.mode);
        if (mode === 2) this.ensureModel(model);

        new Uint8Array(
          this.memory.buffer,
          this.exports.gameai_input_board_ptr(),
          game.board.length,
        ).set(game.board);
        new Uint8Array(
          this.memory.buffer,
          this.exports.gameai_input_meta_ptr(),
          game.metaBoard.length,
        ).set(game.metaBoard);

        const initStatus = this.exports.gameai_init(
          game.nextBoardRow,
          game.nextBoardCol,
          game.currentPlayer,
          game.step,
          mode,
          request.playouts,
          Number.isFinite(Number(request.cPuct)) ? Number(request.cPuct) : 0.3,
          request.rolloutLimit,
          Number.isFinite(Number(request.policyExponent)) ? Number(request.policyExponent) : 0.5,
          request.seed,
          request.rootSelection === "q" || mode === 2 ? 1 : 0,
        );
        if (initStatus === 1) {
          return { action: -1, rootVisits: 0, nodeCount: 1, winner: game.getDoneWinner().winner, q: 0 };
        }
        if (initStatus !== 0) {
          throw new Error(`WASM 搜索初始化失败 (${initStatus})。`);
        }

        const total = this.exports.gameai_requested();
        const chunkSize = Math.max(1, Math.min(4096, Math.floor(hooks.chunkSize || 256)));
        while (this.exports.gameai_completed() < total) {
          if (hooks.isCancelled && hooks.isCancelled()) throw new Error("SEARCH_CANCELLED");
          const completed = this.exports.gameai_run(chunkSize);
          if (hooks.onProgress) hooks.onProgress(completed, total);
          if (this.exports.gameai_status() !== 0) {
            throw new Error(`WASM 搜索中止 (${this.exports.gameai_status()})。`);
          }
          await new Promise((resolve) => setTimeout(resolve, 0));
        }
        if (hooks.isCancelled && hooks.isCancelled()) throw new Error("SEARCH_CANCELLED");
        const action = this.exports.gameai_action();
        return {
          action,
          rootVisits: this.exports.gameai_root_visits(),
          nodeCount: this.exports.gameai_node_count(),
          q: this.exports.gameai_action_q(),
          winner: 0,
        };
      });
    }
  }

  async function runSearch(game, request, model, hooks) {
    const search = await loadEngine(request.wasmUrl);
    return search.search(game, request, model, hooks);
  }

  const api = {
    runSearch,
    packedWeights,
    MODEL_TENSOR_ORDER,
  };
  root.GameAIWasmRuntime = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis === "undefined" ? this : globalThis);
