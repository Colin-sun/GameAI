(function (root) {
  "use strict";

  let modelPromise = null;

  function modelFor(url) {
    if (!modelPromise || modelPromise.url !== url) {
      const promise = root.GameAIPriorModel.PriorModel.load(url);
      promise.url = url;
      modelPromise = promise;
    }
    return modelPromise;
  }

  async function runSearch(request, emit, isCancelled) {
    const engine = root.GameAIEngine;
    const parsed = engine.parseSiteMoves(request.moves);
    if (!parsed.ok) throw new Error(parsed.error);
    const game = engine.GameState.fromSiteMoves(parsed.actions);
    let model = null;
    if (request.mode === "prior") {
      if (!request.modelUrl) throw new Error("The prior model URL is missing.");
      emit({ type: "model-loading", id: request.id });
      model = await modelFor(request.modelUrl);
      emit({ type: "model-ready", id: request.id, metadata: model.metadata });
    }
    const search = new engine.MCTS({
      mode: request.mode,
      playouts: request.playouts,
      cPuct: request.cPuct,
      rolloutLimit: request.rolloutLimit,
      seed: request.seed,
      rootSelection: request.mode === "prior" ? "q" : "visits",
    });
    const result = await search.search(game, model, {
      chunkSize: request.chunkSize || 16,
      isCancelled,
      onProgress: (completed, total) => emit({
        type: "progress",
        id: request.id,
        completed,
        total,
      }),
    });
    if (isCancelled()) throw new Error("SEARCH_CANCELLED");
    const siteAction = result.action >= 0 ? engine.nativeActionToSite(result.action) : -1;
    return {
      action: result.action,
      siteAction,
      label: siteAction >= 0 ? engine.siteActionLabel(siteAction) : "--",
      rootVisits: result.rootVisits,
      nodeCount: result.nodeCount,
      q: result.q,
      winner: result.winner,
      mode: request.mode,
      metadata: model ? model.metadata : null,
    };
  }

  root.GameAISearchRuntime = { runSearch };
})(typeof globalThis === "undefined" ? this : globalThis);
