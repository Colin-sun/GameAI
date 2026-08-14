const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

test("MV3 manifest exposes the worker, popup, hook, and model resource", () => {
  const root = path.join(__dirname, "..");
  const manifest = JSON.parse(fs.readFileSync(path.join(root, "manifest.json"), "utf8"));
  const popup = fs.readFileSync(path.join(root, "panel.html"), "utf8");
  assert.equal(manifest.manifest_version, 3);
  assert.equal(manifest.background.service_worker, "background.js");
  assert.equal(manifest.action.default_popup, "panel.html");
  assert.ok(manifest.content_scripts.some((script) => script.js.includes("page-hook.js") && script.world === "MAIN"));
  assert.ok(manifest.content_scripts.some((script) => script.js.includes("content.js") && script.js.includes("engine.js")));
  assert.ok(manifest.web_accessible_resources.some((entry) => entry.resources.includes("models/*.bin")));
  for (const mode of ["pure", "tactical", "prior"]) {
    assert.match(fs.readFileSync(path.join(root, "content.js"), "utf8"), new RegExp(mode));
    assert.match(popup, new RegExp(`option value="${mode}"`));
  }
});
