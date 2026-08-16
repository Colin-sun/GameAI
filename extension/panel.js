(function () {
  "use strict";

  const DEFAULT_CONFIG = {
    mode: "tactical",
    autoPlay: true,
    aiPlayer: 2,
    playouts: 3000,
    cPuct: 0.4,
    rolloutLimit: 32,
    policyExponent: 0.5,
    rootSelection: "visits",
    seed: 20260811,
  };
  let activeTabId = null;
  let currentState = null;

  const $ = (selector) => document.querySelector(selector);

  function setStatus(message, error) {
    const status = $("[data-status]");
    status.textContent = message;
    status.dataset.phase = error ? "error" : "idle";
  }

  function send(message, callback) {
    if (activeTabId == null) {
      setStatus("请先打开 game.hullqin.cn/jzq", true);
      return;
    }
    chrome.tabs.sendMessage(activeTabId, message, (response) => {
      if (chrome.runtime.lastError || !response) {
        setStatus("当前标签页未连接扩展", true);
        return;
      }
      update(response);
      if (callback) callback(response);
    });
  }

  function update(state) {
    if (!state) return;
    currentState = state;
    const status = $("[data-status]");
    status.textContent = state.error && state.phase === "error" ? state.error : statusText(state);
    status.dataset.phase = state.phase || "idle";
    $("[data-backend]").textContent = state.backend === "js-fallback"
      ? "JS fallback：" + (state.fallbackReason || "WASM 搜索不可用")
      : (state.backend === "wasm" ? "WASM 搜索" : "");
    $("[data-backend]").dataset.backend = state.backend || "";
    $("[data-detail]").textContent = state.hasGame
      ? `${state.moveCount} 手 · ${state.currentPlayer === state.config.aiPlayer ? "AI" : "你"} · 可走 ${state.legalCount}`
      : "";
    const progress = $("[data-progress]");
    const bar = $("[data-progress-bar]");
    if (state.progress) {
      progress.hidden = false;
      bar.style.width = `${Math.min(100, state.progress.completed / Math.max(1, state.progress.total) * 100)}%`;
    } else {
      progress.hidden = true;
      bar.style.width = "0%";
    }
    const config = state.config || DEFAULT_CONFIG;
    $("[data-field='mode']").value = config.mode;
    $("[data-field='aiPlayer']").value = String(config.aiPlayer);
    $("[data-field='autoPlay']").checked = config.autoPlay;
    $("[data-field='playouts']").value = config.playouts;
    $("[data-field='cPuct']").value = config.cPuct;
    $("[data-field='rolloutLimit']").value = config.rolloutLimit;
    $("[data-action='suggest']").disabled = !state.supported || state.phase === "searching" || state.phase === "applying";
    $("[data-action='cancel']").hidden = state.phase !== "searching";
    $("[data-action='open']").hidden = state.route !== "local-home";
    $("[data-action='rule0']").hidden = !(state.route === "local" && state.rule === 1);
    $("[data-action='apply']").hidden = !state.suggestion;
    $("[data-action='apply']").disabled = !state.suggestion || state.phase === "searching";
    $("[data-suggestion]").hidden = !state.suggestion;
    $("[data-suggestion]").textContent = state.suggestion
      ? `建议 ${state.suggestion.label} · Q ${Number(state.suggestion.q || 0).toFixed(3)}`
      : "";
    if (state.suggestion && Number.isFinite(state.suggestion.elapsedMs)) {
      $("[data-suggestion]").textContent += ` · ${state.suggestion.elapsedMs} ms`;
    }
    $("[data-model]").textContent = config.mode === "native-prior"
      ? (state.modelMetadata ? `${state.modelMetadata.checkpoint} · ${state.modelMetadata.channels}c / ${state.modelMetadata.blocks} blocks` : "AI 模型按需加载")
      : "";
  }

  function statusText(state) {
    if (state.route === "room") return "联机房间暂不接管";
    if (state.route === "unsupported") return "请在 /jzq 本地对战页面使用";
    if (state.route === "local-home") return "请打开本地对战棋盘";
    if (!state.hasGame) return state.error || "棋局尚未就绪";
    if (state.phase === "searching") {
      return state.progress ? `搜索中 ${state.progress.completed}/${state.progress.total}` : "正在加载搜索";
    }
    if (state.phase === "applying") return "正在落子";
    if (state.phase === "suggested" && state.suggestion) return `建议 ${state.suggestion.label}`;
    if (state.done) return state.winner === -1 ? "引擎判定平局" : `引擎判定 ${state.winner === 1 ? "先手" : "后手"}胜`;
    return state.currentPlayer === state.config.aiPlayer ? "轮到 AI" : "轮到你";
  }

  function saveConfig() {
    send({
      type: "setConfig",
      config: {
        mode: $("[data-field='mode']").value,
        autoPlay: $("[data-field='autoPlay']").checked,
        aiPlayer: $("[data-field='aiPlayer']").value,
        playouts: $("[data-field='playouts']").value,
        cPuct: $("[data-field='cPuct']").value,
        rolloutLimit: $("[data-field='rolloutLimit']").value,
      },
    });
  }

  function bind() {
    ["mode", "aiPlayer", "autoPlay", "playouts", "cPuct", "rolloutLimit"].forEach((name) => {
      $( `[data-field='${name}']` ).addEventListener("change", saveConfig);
    });
    $("[data-action='suggest']").addEventListener("click", () => send({ type: "search", origin: "manual" }));
    $("[data-action='cancel']").addEventListener("click", () => send({ type: "cancel" }));
    $("[data-action='open']").addEventListener("click", () => send({ type: "open" }));
    $("[data-action='rule0']").addEventListener("click", () => send({ type: "setRule0" }));
    $("[data-action='apply']").addEventListener("click", () => send({ type: "apply" }));
    chrome.runtime.onMessage.addListener((message) => {
      if (message && message.type === "state") update(message.state);
    });
  }

  chrome.tabs.query({ active: true, lastFocusedWindow: true }, (tabs) => {
    activeTabId = tabs[0] && tabs[0].id != null ? tabs[0].id : null;
    bind();
    send({ type: "getState" });
  });
})();
