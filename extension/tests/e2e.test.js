const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");

let playwright;
try {
  playwright = require("playwright");
} catch (_error) {
  playwright = null;
}

const enabled = process.env.RUN_EXTENSION_E2E === "1";

const FIXTURE = `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><title>fixture</title></head>
<body><div id="root"></div>
<script>
(function () {
  const NS = "http://www.w3.org/2000/svg";
  function moves() { return new URL(location.href).searchParams.get("p") || ""; }
  function render() {
    const root = document.getElementById("root");
    const svg = document.createElementNS(NS, "svg");
    svg.id = "svg";
    svg.setAttribute("viewBox", "-53 -53 106 106");
    const defs = document.createElementNS(NS, "defs");
    ["black", "white"].forEach((id) => {
      const group = document.createElementNS(NS, "g");
      group.id = id;
      const rect = document.createElementNS(NS, "rect");
      rect.setAttribute("x", "-4"); rect.setAttribute("y", "-4");
      rect.setAttribute("width", "8"); rect.setAttribute("height", "8");
      rect.setAttribute("fill", id === "black" ? "#f53f3f" : "#165dff");
      group.appendChild(rect); defs.appendChild(group);
    });
    svg.appendChild(defs);
    const sequence = moves();
    const occupied = new Set();
    for (let index = 0; index < sequence.length; index += 2) {
      const action = "abcdefghi".indexOf(sequence[index]) + 9 * (Number(sequence[index + 1]) - 1);
      occupied.add(action);
      const piece = document.createElementNS(NS, "use");
      piece.setAttribute("href", "#" + (index % 4 ? "white" : "black"));
      piece.setAttribute("x", String(action % 9 * 10 - 40));
      piece.setAttribute("y", String(40 - 10 * Math.floor(action / 9)));
      svg.appendChild(piece);
    }
    for (let action = 0; action < 81; action += 1) {
      if (occupied.has(action)) continue;
      const piece = document.createElementNS(NS, "use");
      piece.id = "piece-" + action;
      piece.classList.add("jzq-undropped-piece");
      piece.setAttribute("x", String(action % 9 * 10 - 40));
      piece.setAttribute("y", String(40 - 10 * Math.floor(action / 9)));
      piece.addEventListener("click", function () {
        const url = new URL(location.href);
        url.searchParams.set("p", moves() + "abcdefghi"[action % 9] + (Math.floor(action / 9) + 1));
        history.replaceState({}, "", url.pathname + url.search);
        window.dispatchEvent(new PopStateEvent("popstate"));
      });
      svg.appendChild(piece);
    }
    root.replaceChildren(svg);
  }
  window.addEventListener("popstate", render);
  render();
})();
</script></body></html>`;

test("loads the MV3 extension and exercises all three search modes", { skip: !enabled || !playwright }, async () => {
  const extensionPath = path.resolve(__dirname, "..");
  const context = await playwright.chromium.launchPersistentContext("", {
    // Chromium's headless shell disables extensions.  Use the regular browser
    // in its new headless mode so the MV3 service worker is actually loaded.
    headless: false,
    args: [
      "--no-sandbox",
      "--headless=new",
      `--disable-extensions-except=${extensionPath}`,
      `--load-extension=${extensionPath}`,
    ],
  });
  try {
    const page = await context.newPage();
    await page.route("https://game.hullqin.cn/jzq*", (route) => route.fulfill({
      status: 200,
      contentType: "text/html",
      body: FIXTURE,
    }));
    await page.goto("https://game.hullqin.cn/jzq?p=", { waitUntil: "domcontentloaded" });
    const panel = page.locator("gameai-panel");
    await panel.waitFor({ state: "visible", timeout: 10000 });
    assert.equal(await panel.locator("[data-field='mode']").inputValue(), "tactical");
    assert.equal(await panel.locator("[data-field='playouts']").inputValue(), "3000");
    assert.equal(await panel.locator("[data-field='cPuct']").inputValue(), "0.4");
    assert.equal(await panel.locator("[data-field='rolloutLimit']").inputValue(), "32");
    assert.deepEqual(
      await panel.locator("[data-field='mode'] option").allTextContents(),
      ["简单（均匀搜索）", "中等（战术搜索）", "困难（AI 模型）"],
    );
    const status = panel.locator("[data-status]");
    await panel.locator("[data-field='autoPlay']").uncheck();
    await panel.locator("[data-field='playouts']").fill("128");
    await panel.locator("[data-field='playouts']").press("Tab");

    const expected = {
      uniform: ["6200", "0.8", "300"],
      tactical: ["3000", "0.4", "32"],
      "native-prior": ["12000", "0.2", "32"],
    };
    for (const mode of ["uniform", "tactical", "native-prior"]) {
      await panel.locator("[data-field='mode']").selectOption(mode);
      assert.deepEqual(
        await Promise.all([
          panel.locator("[data-field='playouts']").inputValue(),
          panel.locator("[data-field='cPuct']").inputValue(),
          panel.locator("[data-field='rolloutLimit']").inputValue(),
        ]),
        expected[mode],
      );
      await panel.locator("[data-action='suggest']").click();
      await page.locator("#gameai-search-lock").waitFor({ state: "attached", timeout: 5000 });
      await page.waitForFunction(() => {
        const element = document.querySelector("gameai-panel");
        return element && /建议/.test(element.shadowRoot.querySelector("[data-status]").textContent);
      }, null, { timeout: mode === "prior" ? 15000 : 5000 });
      assert.match(await status.textContent(), /建议/);
      assert.ok(await page.locator("#gameai-suggestion-overlay").count());
      assert.equal(await panel.locator("[data-backend]").textContent(), "WASM 搜索");
      const timing = (await panel.locator("[data-suggestion]").textContent()).match(/(\d+) ms/);
      assert.ok(timing, `missing search timing for ${mode}`);
      assert.ok(Number(timing[1]) < 1000, `${mode} search took ${timing[1]} ms`);
    }
    assert.match(await panel.locator("[data-model]").textContent(), /128c/);

    await panel.locator("[data-field='aiPlayer']").selectOption("1");
    await panel.locator("[data-field='autoPlay']").check();
    await panel.locator("[data-field='mode']").selectOption("uniform");
    await panel.locator("[data-field='playouts']").fill("2");
    await panel.locator("[data-field='playouts']").press("Tab");
    await page.waitForFunction(() => {
      const value = new URL(location.href).searchParams.get("p");
      return value && value.length === 2;
    }, null, { timeout: 5000 });
    assert.equal((await page.url()).includes("/jzq?p="), true);

    await page.goto("https://game.hullqin.cn/jzq/ROOM", { waitUntil: "domcontentloaded" });
    await page.waitForFunction(() => {
      const element = document.querySelector("gameai-panel");
      return element && getComputedStyle(element).display === "none";
    }, null, { timeout: 5000 });
  } finally {
    await context.close();
  }
});
