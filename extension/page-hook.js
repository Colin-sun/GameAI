(function () {
  "use strict";

  if (window.__GAMEAI_PAGE_HOOK__) return;
  window.__GAMEAI_PAGE_HOOK__ = true;

  const SOURCE = "gameai-page-hook-v1";

  function report(reason) {
    window.postMessage({
      source: SOURCE,
      type: "location",
      href: window.location.href,
      reason: reason || "history",
    }, "*");
  }

  function wrapHistoryMethod(name) {
    const original = window.history[name];
    if (typeof original !== "function") return;
    window.history[name] = function () {
      const result = original.apply(this, arguments);
      report(name);
      return result;
    };
  }

  wrapHistoryMethod("pushState");
  wrapHistoryMethod("replaceState");
  window.addEventListener("popstate", () => report("popstate"));

  function validMoves(moves) {
    return typeof moves === "string" &&
      moves.length % 2 === 0 &&
      /^(?:[a-i][1-9])*$/.test(moves);
  }

  function navigate(data) {
    if (!validMoves(data.moves)) return;
    const url = new URL(window.location.href);
    if (url.pathname !== "/jzq" && url.pathname !== "/jzq/") return;
    url.searchParams.set("p", data.moves);
    if (data.rule === 0) url.searchParams.delete("r");
    if (data.rule === 1) url.searchParams.set("r", "1");
    const method = data.replace ? "replaceState" : "pushState";
    window.history[method]({}, "", `${url.pathname}${url.search}${url.hash}`);
    window.dispatchEvent(new PopStateEvent("popstate"));
  }

  window.addEventListener("message", (event) => {
    if (event.source !== window || !event.data || event.data.source !== SOURCE) return;
    if (event.data.type === "request-location") {
      report("request");
    } else if (event.data.type === "navigate") {
      navigate(event.data);
    }
  });

  report("initial");
})();
