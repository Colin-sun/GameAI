importScripts("engine.js", "model.js");
importScripts("search-runtime.js");

(function () {
  "use strict";

  const cancelled = new Set();

  function send(message) {
    self.postMessage(message);
  }

  self.onmessage = (event) => {
    const request = event.data || {};
    if (request.type === "cancel") {
      cancelled.add(request.id);
      return;
    }
    if (request.type !== "search" || request.id == null) return;
    cancelled.delete(request.id);
    self.GameAISearchRuntime.runSearch(
      request,
      (message) => send(message),
      () => cancelled.has(request.id),
    ).then((result) => {
      send({ type: "result", id: request.id, result });
    }).catch((error) => {
      send({
        type: "error",
        id: request.id,
        error: error && error.message ? error.message : String(error),
      });
    }).finally(() => {
      cancelled.delete(request.id);
    });
  };
})();
