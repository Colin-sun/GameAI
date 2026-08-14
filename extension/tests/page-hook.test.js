const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

function makePage() {
  const listeners = new Map();
  const reports = [];
  const location = { href: "https://game.hullqin.cn/jzq?p=a1&r=1" };
  const window = {
    location,
    history: {},
    __GAMEAI_PAGE_HOOK__: false,
    addEventListener(type, listener) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(listener);
    },
    dispatchEvent(event) {
      for (const listener of listeners.get(event.type) || []) listener(event);
    },
    postMessage(data) {
      reports.push(data);
    },
  };
  for (const method of ["pushState", "replaceState"]) {
    window.history[method] = (_state, _title, next) => {
      location.href = new URL(next, location.href).href;
    };
  }
  window.PopStateEvent = class PopStateEvent { constructor(type) { this.type = type; } };
  window.URL = URL;
  const context = vm.createContext({ window, URL, PopStateEvent: window.PopStateEvent });
  vm.runInContext(fs.readFileSync("extension/page-hook.js", "utf8"), context);
  return { window, reports, dispatchMessage(data) {
    window.dispatchEvent({ type: "message", source: window, data });
  } };
}

test("page hook reports SPA history and navigates only the local route", () => {
  const page = makePage();
  assert.equal(page.reports.at(-1).href, "https://game.hullqin.cn/jzq?p=a1&r=1");
  page.dispatchMessage({ source: "gameai-page-hook-v1", type: "navigate", moves: "a1b2", rule: 0 });
  assert.equal(page.window.location.href, "https://game.hullqin.cn/jzq?p=a1b2");
  assert.ok(page.reports.some((report) => report.reason === "pushState"));
  page.window.location.href = "https://game.hullqin.cn/jzq/ROOM";
  page.dispatchMessage({ source: "gameai-page-hook-v1", type: "navigate", moves: "a1", rule: 0 });
  assert.equal(page.window.location.href, "https://game.hullqin.cn/jzq/ROOM");
});

