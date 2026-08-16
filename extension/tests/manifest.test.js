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
  assert.deepEqual(manifest.icons, {
    "16": "icons/icon16.png",
    "32": "icons/icon32.png",
    "48": "icons/icon48.png",
    "128": "icons/icon128.png",
  });
  assert.deepEqual(manifest.action.default_icon, {
    "16": "icons/icon16.png",
    "32": "icons/icon32.png",
    "48": "icons/icon48.png",
  });
  assert.ok(manifest.web_accessible_resources.some((entry) => entry.resources.includes("models/*.bin")));
  assert.ok(manifest.web_accessible_resources.some((entry) => entry.resources.includes("wasm/engine.wasm")));
  for (const mode of ["uniform", "tactical", "native-prior"]) {
    assert.match(fs.readFileSync(path.join(root, "content.js"), "utf8"), new RegExp(mode));
    assert.match(popup, new RegExp(`option value="${mode}"`));
  }
  for (const label of ["简单（均匀搜索）", "中等（战术搜索）", "困难（AI 模型）"]) {
    assert.match(popup, new RegExp(label));
    assert.match(fs.readFileSync(path.join(root, "content.js"), "utf8"), new RegExp(label));
  }
  for (const [name, minimum] of [["icon16.png", 16], ["icon32.png", 32], ["icon48.png", 48], ["icon128.png", 128]]) {
    const icon = fs.readFileSync(path.join(root, "icons", name));
    assert.equal(icon.subarray(0, 8).toString("hex"), "89504e470d0a1a0a");
    assert.ok(icon.readUInt32BE(16) >= minimum);
    assert.ok(icon.readUInt32BE(20) >= minimum);
  }
});
