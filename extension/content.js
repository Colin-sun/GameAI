(function () {
  "use strict";

  const engine = globalThis.GameAIEngine;
  const PAGE_SOURCE = "gameai-page-hook-v1";
  const MODEL_FILE = "models/utt_majority_v1_torch_teacher6000_512_hard.bin";
  const DEFAULT_CONFIG = Object.freeze({
    mode: "tactical",
    autoPlay: true,
    aiPlayer: 2,
    playouts: 256,
    cPuct: 0.8,
    rolloutLimit: 32,
    seed: 20260811,
  });
  const MODES = new Set(["pure", "tactical", "prior"]);
  const MAX_MOVES_LENGTH = engine.ACTION_SIZE * 2;

  const config = { ...DEFAULT_CONFIG };
  const state = {
    route: "unknown",
    href: window.location.href,
    supported: false,
    hasGame: false,
    moves: "",
    rule: 0,
    game: null,
    phase: "idle",
    progress: null,
    suggestion: null,
    error: "",
    modelMetadata: null,
  };

  let requestSerial = 0;
  let activeRequest = null;
  let autoTimer = null;
  let panelHost = null;
  let panelRoot = null;
  let overlayObserver = null;
  let interactionLock = null;
  let configReady = false;

  function clampInteger(value, fallback, minimum, maximum) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    return Math.min(maximum, Math.max(minimum, Math.floor(parsed)));
  }

  function normalizeConfig(values) {
    const source = values || {};
    const mode = MODES.has(source.mode) ? source.mode : DEFAULT_CONFIG.mode;
    const aiPlayer = Number(source.aiPlayer) === 1 ? 1 : 2;
    const cPuct = Number(source.cPuct);
    return {
      mode,
      autoPlay: source.autoPlay !== false,
      aiPlayer,
      playouts: clampInteger(source.playouts, DEFAULT_CONFIG.playouts, 1, 2000),
      cPuct: Number.isFinite(cPuct) ? Math.min(4, Math.max(0.05, cPuct)) : DEFAULT_CONFIG.cPuct,
      rolloutLimit: clampInteger(source.rolloutLimit, DEFAULT_CONFIG.rolloutLimit, 1, 128),
      seed: clampInteger(source.seed, DEFAULT_CONFIG.seed, 1, 0x7fffffff),
    };
  }

  function storageGet() {
    return new Promise((resolve) => {
      chrome.storage.local.get({ config: DEFAULT_CONFIG }, (result) => {
        if (chrome.runtime.lastError) {
          resolve({ ...DEFAULT_CONFIG });
          return;
        }
        resolve(normalizeConfig(result.config));
      });
    });
  }

  function storageSet() {
    chrome.storage.local.set({ config: { ...config } }, () => {
      void chrome.runtime.lastError;
    });
  }

  function snapshot() {
    const game = state.game;
    const done = game ? game.getDoneWinner() : { done: false, winner: 0 };
    return {
      route: state.route,
      href: state.href,
      supported: state.supported,
      hasGame: state.hasGame,
      moves: state.moves,
      moveCount: state.moves.length / 2,
      rule: state.rule,
      currentPlayer: game ? game.currentPlayer : 0,
      done: done.done,
      winner: done.winner,
      legalCount: game ? game.getValidActions().length : 0,
      phase: state.phase,
      progress: state.progress,
      suggestion: state.suggestion,
      error: state.error,
      modelMetadata: state.modelMetadata,
      config: { ...config },
    };
  }

  function notify() {
    renderPanel();
    updateInteractionLock();
    chrome.runtime.sendMessage({ type: "state", state: snapshot() }, () => {
      void chrome.runtime.lastError;
    });
  }

  function updateInteractionLock() {
    const shouldLock = state.phase === "searching" || state.phase === "applying";
    if (shouldLock && !interactionLock) {
      interactionLock = document.createElement("div");
      interactionLock.id = "gameai-search-lock";
      interactionLock.setAttribute("aria-hidden", "true");
      interactionLock.style.cssText = [
        "position: fixed",
        "inset: 0",
        "z-index: 2147483646",
        "background: transparent",
        "cursor: wait",
        "pointer-events: auto",
      ].join(";");
      document.documentElement.appendChild(interactionLock);
    } else if (!shouldLock && interactionLock) {
      interactionLock.remove();
      interactionLock = null;
    }
  }

  function postPageMessage(type, payload) {
    window.postMessage({ source: PAGE_SOURCE, type, ...(payload || {}) }, "*");
  }

  function isLocalPath(pathname) {
    return pathname === "/jzq" || pathname === "/jzq/";
  }

  function cancelSearch(notifyState) {
    if (autoTimer !== null) {
      window.clearTimeout(autoTimer);
      autoTimer = null;
    }
    if (activeRequest) {
      chrome.runtime.sendMessage({ type: "cancelSearch", id: activeRequest.id }, () => {
        void chrome.runtime.lastError;
      });
    }
    activeRequest = null;
    state.progress = null;
    if (state.phase === "searching" || state.phase === "applying") {
      state.phase = "idle";
    }
    if (notifyState) notify();
  }

  function clearOverlay() {
    const old = document.getElementById("gameai-suggestion-overlay");
    if (old) old.remove();
  }

  function drawOverlay() {
    clearOverlay();
    if (!state.suggestion || state.route !== "local" || !state.hasGame) return;
    const svg = document.querySelector("#svg");
    if (!svg) return;
    const action = state.suggestion.siteAction;
    const col = action % engine.BOARD_SIZE;
    const row = Math.floor(action / engine.BOARD_SIZE);
    const ns = "http://www.w3.org/2000/svg";
    const group = document.createElementNS(ns, "g");
    group.id = "gameai-suggestion-overlay";
    group.setAttribute("pointer-events", "none");
    group.setAttribute("aria-label", `GameAI suggestion ${state.suggestion.label}`);
    const rect = document.createElementNS(ns, "rect");
    rect.setAttribute("x", String(col * 10 - 45));
    rect.setAttribute("y", String(35 - row * 10));
    rect.setAttribute("width", "10");
    rect.setAttribute("height", "10");
    rect.setAttribute("fill", "#f5c542");
    rect.setAttribute("fill-opacity", "0.24");
    rect.setAttribute("stroke", "#c98a00");
    rect.setAttribute("stroke-width", "1.2");
    rect.setAttribute("stroke-dasharray", "2 1");
    group.appendChild(rect);
    svg.appendChild(group);
  }

  function watchBoard() {
    if (overlayObserver) overlayObserver.disconnect();
    const svg = document.querySelector("#svg");
    if (!svg) return;
    overlayObserver = new MutationObserver(() => {
      if (state.suggestion && !document.getElementById("gameai-suggestion-overlay")) {
        window.requestAnimationFrame(drawOverlay);
      }
    });
    overlayObserver.observe(svg, { childList: true });
    drawOverlay();
  }

  function startSearch(origin) {
    if (!state.supported || !state.hasGame || !state.game) return;
    if (state.game.getDoneWinner().done) return;
    if (!state.game.getValidActions().length) return;
    cancelSearch(false);
    state.error = "";
    state.suggestion = null;
    clearOverlay();
    const id = ++requestSerial;
    activeRequest = {
      id,
      origin: origin || "manual",
      href: state.href,
      moves: state.moves,
    };
    state.phase = "searching";
    state.progress = { completed: 0, total: config.playouts };
    notify();
    try {
      chrome.runtime.sendMessage({
        type: "searchRequest",
        request: {
          id,
          moves: state.moves,
          mode: config.mode,
          playouts: config.playouts,
          cPuct: config.cPuct,
          rolloutLimit: config.rolloutLimit,
          seed: (config.seed + state.moves.length + id) >>> 0,
          chunkSize: 16,
          modelUrl: chrome.runtime.getURL(MODEL_FILE),
        },
      }, (_response) => {
        if (!chrome.runtime.lastError) return;
        if (!activeRequest || activeRequest.id !== id) return;
        activeRequest = null;
        state.phase = "error";
        state.progress = null;
        state.error = chrome.runtime.lastError.message || "扩展搜索服务不可用。";
        notify();
      });
    } catch (error) {
      activeRequest = null;
      state.phase = "error";
      state.progress = null;
      state.error = error.message || String(error);
      notify();
    }
  }

  function handleWorkerMessage(event) {
    const message = event.data || {};
    if (!activeRequest || message.id !== activeRequest.id) return;
    if (message.type === "model-loading") {
      state.modelMetadata = null;
      notify();
      return;
    }
    if (message.type === "model-ready") {
      state.modelMetadata = message.metadata || null;
      notify();
      return;
    }
    if (message.type === "progress") {
      state.progress = { completed: message.completed, total: message.total };
      renderPanel();
      notify();
      return;
    }
    if (message.type === "error") {
      activeRequest = null;
      state.progress = null;
      if (message.error === "SEARCH_CANCELLED") {
        state.phase = "idle";
      } else {
        state.phase = "error";
        state.error = message.error || "Search failed.";
      }
      notify();
      return;
    }
    if (message.type !== "result") return;

    const request = activeRequest;
    activeRequest = null;
    state.progress = null;
    if (request.href !== state.href || request.moves !== state.moves || !state.game) {
      state.phase = "idle";
      notify();
      return;
    }
    const result = message.result || {};
    if (result.action < 0 || !state.game.isActionValid(result.action)) {
      state.phase = "idle";
      state.error = "当前局面没有可执行的合法动作。";
      notify();
      return;
    }
    const siteAction = engine.nativeActionToSite(result.action);
    const label = engine.siteActionLabel(siteAction);
    state.suggestion = {
      action: result.action,
      siteAction,
      label,
      q: result.q,
      rootVisits: result.rootVisits,
      nodeCount: result.nodeCount,
      mode: result.mode,
    };
    state.modelMetadata = result.metadata || state.modelMetadata;
    if (request.origin === "auto" && config.autoPlay &&
        state.game.currentPlayer === config.aiPlayer) {
      state.phase = "applying";
      notify();
      postPageMessage("navigate", {
        moves: state.moves + label,
        rule: 0,
        replace: false,
      });
      return;
    }
    state.phase = "suggested";
    watchBoard();
    notify();
  }

  function scheduleAutoSearch() {
    if (autoTimer !== null) {
      window.clearTimeout(autoTimer);
      autoTimer = null;
    }
    if (!configReady || !config.autoPlay || !state.supported || !state.hasGame || !state.game) return;
    if (state.game.getDoneWinner().done || state.game.currentPlayer !== config.aiPlayer) return;
    autoTimer = window.setTimeout(() => {
      autoTimer = null;
      startSearch("auto");
    }, 120);
  }

  function syncLocation(href) {
    const nextHref = String(href || window.location.href);
    if (nextHref === state.href && state.route !== "unknown") return;
    cancelSearch(false);
    state.href = nextHref;
    state.route = "unsupported";
    state.supported = false;
    state.hasGame = false;
    state.moves = "";
    state.rule = 0;
    state.game = null;
    state.suggestion = null;
    state.error = "";
    clearOverlay();
    try {
      const url = new URL(nextHref);
      if (!isLocalPath(url.pathname)) {
        state.route = url.pathname.startsWith("/jzq/") ? "room" : "unsupported";
      } else if (!url.searchParams.has("p")) {
        state.route = "local-home";
      } else {
        const rawRule = url.searchParams.get("r");
        if (rawRule !== null && rawRule !== "0" && rawRule !== "1") {
          state.error = "只支持网页的 r=0 或 r=1 规则参数。";
        } else if (rawRule === "1") {
          state.rule = 1;
          state.error = "当前 r=1 不是 majority-utt-v1；请切换为 r=0。";
        }
        state.moves = url.searchParams.get("p") || "";
        if (state.moves.length > MAX_MOVES_LENGTH) {
          state.error = "p 走子序列超过 81 步。";
        }
        const parsed = engine.parseSiteMoves(state.moves);
        if (!parsed.ok) {
          state.error = parsed.error;
        } else {
          try {
            state.game = engine.GameState.fromSiteMoves(parsed.actions);
            state.hasGame = true;
          } catch (error) {
            state.error = error.message || String(error);
          }
        }
        state.route = "local";
        state.supported = state.rule === 0 && state.hasGame && !state.error;
      }
    } catch (error) {
      state.error = error.message || String(error);
    }
    watchBoard();
    notify();
    scheduleAutoSearch();
  }

  function applySuggestion() {
    if (!state.suggestion || !state.supported || !state.game) return;
    if (!state.game.isActionValid(state.suggestion.action)) {
      state.error = "建议着已因局面变化失效。";
      state.suggestion = null;
      notify();
      return;
    }
    state.phase = "applying";
    notify();
    postPageMessage("navigate", {
      moves: state.moves + state.suggestion.label,
      rule: 0,
      replace: false,
    });
  }

  function setConfig(values) {
    Object.assign(config, normalizeConfig({ ...config, ...(values || {}) }));
    storageSet();
    renderPanel();
    notify();
    scheduleAutoSearch();
  }

  function statusText() {
    if (state.route === "room") return "联机房间暂不接管";
    if (state.route === "unsupported") return "请在 /jzq 本地对战页面使用";
    if (state.route === "local-home") return "请打开本地对战棋盘";
    if (!state.hasGame) return state.error || "棋局尚未就绪";
    if (state.phase === "searching") {
      const progress = state.progress;
      return progress ? `搜索中 ${progress.completed}/${progress.total}` : "正在加载搜索";
    }
    if (state.phase === "applying") return "正在落子";
    if (state.phase === "error") return state.error || "搜索失败";
    if (state.phase === "suggested" && state.suggestion) {
      return `建议 ${state.suggestion.label}`;
    }
    const done = state.game.getDoneWinner();
    if (done.done) {
      return done.winner === -1 ? "引擎判定平局" : `引擎判定 ${done.winner === 1 ? "先手" : "后手"}胜`;
    }
    return state.game.currentPlayer === config.aiPlayer ? "轮到 AI" : "轮到你";
  }

  function currentPlayerText() {
    if (!state.game || !state.hasGame) return "--";
    return state.game.currentPlayer === config.aiPlayer ? "AI" : "你";
  }

  function renderPanel() {
    if (!panelRoot) return;
    panelHost.style.display = state.route === "local" || state.route === "local-home" ? "block" : "none";
    const $ = (selector) => panelRoot.querySelector(selector);
    const status = $("[data-status]");
    const detail = $("[data-detail]");
    const progress = $("[data-progress]");
    const progressBar = $("[data-progress-bar]");
    const suggestion = $("[data-suggestion]");
    const suggestionValue = $("[data-suggestion-value]");
    const suggestButton = $("[data-action='suggest']");
    const applyButton = $("[data-action='apply']");
    const cancelButton = $("[data-action='cancel']");
    const openButton = $("[data-action='open']");
    const ruleButton = $("[data-action='rule0']");
    if (!status) return;

    status.textContent = statusText();
    status.dataset.phase = state.phase;
    detail.textContent = state.hasGame
      ? `${state.moves.length / 2} 手 · ${currentPlayerText()} · 可走 ${state.game.getValidActions().length}`
      : "";
    if (state.progress) {
      progress.hidden = false;
      const ratio = state.progress.total > 0 ? state.progress.completed / state.progress.total : 0;
      progressBar.style.width = `${Math.max(0, Math.min(100, ratio * 100))}%`;
    } else {
      progress.hidden = true;
      progressBar.style.width = "0%";
    }
    suggestion.hidden = !(state.phase === "suggested" && state.suggestion);
    if (state.suggestion && suggestionValue) {
      suggestionValue.textContent = `${state.suggestion.label}  ·  Q ${Number(state.suggestion.q || 0).toFixed(3)}`;
    }
    suggestButton.disabled = !state.supported || state.phase === "searching" || state.phase === "applying";
    applyButton.disabled = !state.suggestion || state.phase === "searching";
    cancelButton.hidden = state.phase !== "searching";
    openButton.hidden = state.route !== "local-home";
    ruleButton.hidden = !(state.route === "local" && state.rule === 1);

    const mode = $("[data-field='mode']");
    const auto = $("[data-field='autoPlay']");
    const aiPlayer = $("[data-field='aiPlayer']");
    const playouts = $("[data-field='playouts']");
    const cPuct = $("[data-field='cPuct']");
    const rolloutLimit = $("[data-field='rolloutLimit']");
    if (mode && mode.value !== config.mode) mode.value = config.mode;
    if (auto) auto.checked = config.autoPlay;
    if (aiPlayer && aiPlayer.value !== String(config.aiPlayer)) aiPlayer.value = String(config.aiPlayer);
    if (playouts && document.activeElement !== playouts) playouts.value = String(config.playouts);
    if (cPuct && document.activeElement !== cPuct) cPuct.value = String(config.cPuct);
    if (rolloutLimit && document.activeElement !== rolloutLimit) rolloutLimit.value = String(config.rolloutLimit);

    const modelNote = $("[data-model]");
    if (modelNote) {
      modelNote.textContent = config.mode === "prior"
        ? (state.modelMetadata ? `${state.modelMetadata.channels}c / ${state.modelMetadata.blocks} blocks` : "模型按需加载")
        : "未使用 prior model";
    }
  }

  function bindPanelEvents() {
    const on = (selector, event, callback) => {
      const element = panelRoot.querySelector(selector);
      if (element) element.addEventListener(event, callback);
    };
    on("[data-action='suggest']", "click", () => startSearch("manual"));
    on("[data-action='apply']", "click", applySuggestion);
    on("[data-action='cancel']", "click", () => cancelSearch(true));
    on("[data-action='open']", "click", () => postPageMessage("navigate", {
      moves: "",
      rule: 0,
      replace: true,
    }));
    on("[data-action='rule0']", "click", () => postPageMessage("navigate", {
      moves: state.moves,
      rule: 0,
      replace: true,
    }));
    on("[data-action='minimize']", "click", () => {
      panelHost.classList.toggle("collapsed");
    });
    on("[data-field='mode']", "change", (event) => setConfig({ mode: event.target.value }));
    on("[data-field='autoPlay']", "change", (event) => setConfig({ autoPlay: event.target.checked }));
    on("[data-field='aiPlayer']", "change", (event) => setConfig({ aiPlayer: event.target.value }));
    on("[data-field='playouts']", "change", (event) => setConfig({ playouts: event.target.value }));
    on("[data-field='cPuct']", "change", (event) => setConfig({ cPuct: event.target.value }));
    on("[data-field='rolloutLimit']", "change", (event) => setConfig({ rolloutLimit: event.target.value }));
  }

  function createPanel() {
    panelHost = document.createElement("gameai-panel");
    panelHost.style.cssText = [
      "all: initial",
      "position: fixed",
      "right: 16px",
      "bottom: 16px",
      "z-index: 2147483647",
      "display: none",
    ].join(";");
    document.documentElement.appendChild(panelHost);
    panelRoot = panelHost.attachShadow({ mode: "open" });
    panelRoot.innerHTML = `
      <style>
        :host { all: initial; }
        .panel {
          box-sizing: border-box;
          width: min(320px, calc(100vw - 24px));
          color: #17202b;
          background: #fbfcfd;
          border: 1px solid #cbd3dc;
          border-radius: 8px;
          box-shadow: 0 12px 32px rgba(23, 32, 43, .22);
          font: 13px/1.4 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
          padding: 12px;
        }
        .collapsed-button { display: none; }
        :host(.collapsed) .panel { display: none; }
        :host(.collapsed) .collapsed-button {
          display: block;
          border: 1px solid #cbd3dc;
          border-radius: 999px;
          background: #fbfcfd;
          color: #17202b;
          box-shadow: 0 8px 24px rgba(23, 32, 43, .2);
          padding: 8px 11px;
          font: 600 12px/1 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
          cursor: pointer;
        }
        .bar, .actions, .suggestion-line { display: flex; align-items: center; gap: 8px; }
        .bar { justify-content: space-between; margin-bottom: 10px; }
        .brand { display: flex; align-items: center; gap: 7px; font-weight: 700; letter-spacing: .02em; }
        .mark { width: 9px; height: 9px; border-radius: 50%; background: #165dff; box-shadow: 13px 0 0 #f53f3f; margin-right: 11px; }
        button, select, input { font: inherit; }
        button { cursor: pointer; }
        .icon-button { border: 0; background: transparent; color: #66717d; width: 24px; height: 24px; font-size: 17px; line-height: 1; }
        .status { border-left: 3px solid #8994a1; padding: 7px 9px; background: #f0f3f6; font-weight: 650; }
        .status[data-phase="searching"] { border-left-color: #165dff; }
        .status[data-phase="suggested"] { border-left-color: #c98a00; }
        .status[data-phase="error"] { border-left-color: #c73c3c; color: #9e2525; }
        .detail { color: #66717d; min-height: 18px; margin: 5px 0 9px; }
        .progress { height: 3px; background: #dce2e8; overflow: hidden; margin: -3px 0 9px; }
        .progress-bar { height: 100%; width: 0; background: #165dff; transition: width .12s linear; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px 10px; }
        label { display: grid; gap: 3px; color: #596572; font-size: 11px; }
        select, input[type="number"] { width: 100%; box-sizing: border-box; min-height: 30px; border: 1px solid #cbd3dc; border-radius: 5px; padding: 4px 6px; background: white; color: #17202b; }
        .toggle { display: flex; align-items: center; gap: 6px; min-height: 30px; }
        .toggle input { accent-color: #165dff; }
        .actions { margin-top: 10px; }
        .primary, .secondary { border-radius: 5px; min-height: 32px; padding: 0 10px; font-weight: 650; }
        .primary { flex: 1; border: 1px solid #165dff; background: #165dff; color: white; }
        .secondary { border: 1px solid #b9c3ce; background: white; color: #253241; }
        button:disabled { cursor: default; opacity: .45; }
        .suggestion-line { justify-content: space-between; margin-top: 9px; padding: 7px 8px; border: 1px solid #e0c36b; background: #fff8dc; color: #6b4d00; }
        .model { color: #8994a1; font-size: 10px; margin-top: 8px; }
        @media (max-width: 480px) { .panel { width: calc(100vw - 16px); } }
      </style>
      <button class="collapsed-button" data-action="minimize" title="展开 GameAI">GameAI</button>
      <section class="panel" aria-label="GameAI">
        <header class="bar">
          <div class="brand"><span class="mark"></span><span>GameAI</span></div>
          <button class="icon-button" data-action="minimize" title="收起面板" aria-label="收起面板">−</button>
        </header>
        <div class="status" data-status>正在连接棋盘</div>
        <div class="detail" data-detail></div>
        <div class="progress" data-progress hidden><div class="progress-bar" data-progress-bar></div></div>
        <div class="grid">
          <label>搜索模式
            <select data-field="mode">
              <option value="pure">Pure MCTS</option>
              <option value="tactical">Tactical MCTS</option>
              <option value="prior">Prior model</option>
            </select>
          </label>
          <label>AI 方
            <select data-field="aiPlayer">
              <option value="1">先手</option>
              <option value="2">后手</option>
            </select>
          </label>
          <label>自动人机
            <span class="toggle"><input type="checkbox" data-field="autoPlay"><span>轮到 AI 自动落子</span></span>
          </label>
          <label>模拟次数
            <input data-field="playouts" type="number" min="1" max="2000" step="1">
          </label>
          <label>探索系数
            <input data-field="cPuct" type="number" min="0.05" max="4" step="0.05">
          </label>
          <label>Rollout 上限
            <input data-field="rolloutLimit" type="number" min="1" max="128" step="1">
          </label>
        </div>
        <div class="actions">
          <button class="primary" data-action="suggest">搜索建议</button>
          <button class="secondary" data-action="cancel" hidden>停止</button>
          <button class="secondary" data-action="open" hidden>打开棋盘</button>
          <button class="secondary" data-action="rule0" hidden>切换 r=0</button>
        </div>
        <div class="suggestion-line" data-suggestion hidden>
          <span data-suggestion-value></span>
          <button class="secondary" data-action="apply">采用</button>
        </div>
        <div class="model" data-model></div>
      </section>`;
    bindPanelEvents();
    renderPanel();
  }

  function handleRuntimeMessage(message, _sender, sendResponse) {
    if (!message || !message.type) return false;
    if (message.type === "aiWorker") {
      handleWorkerMessage({ data: message.message });
      return false;
    }
    if (message.type === "getState") {
      sendResponse(snapshot());
      return false;
    }
    if (message.type === "setConfig") {
      setConfig(message.config);
      sendResponse(snapshot());
      return false;
    }
    if (message.type === "search") {
      startSearch(message.origin || "manual");
      sendResponse(snapshot());
      return false;
    }
    if (message.type === "cancel") {
      cancelSearch(true);
      sendResponse(snapshot());
      return false;
    }
    if (message.type === "apply") {
      applySuggestion();
      sendResponse(snapshot());
      return false;
    }
    if (message.type === "open") {
      postPageMessage("navigate", { moves: "", rule: 0, replace: true });
      sendResponse(snapshot());
      return false;
    }
    if (message.type === "setRule0") {
      postPageMessage("navigate", { moves: state.moves, rule: 0, replace: true });
      sendResponse(snapshot());
      return false;
    }
    return false;
  }

  function start() {
    createPanel();
    chrome.runtime.onMessage.addListener(handleRuntimeMessage);
    window.addEventListener("message", (event) => {
      if (event.source !== window || !event.data || event.data.source !== PAGE_SOURCE) return;
      if (event.data.type === "location") syncLocation(event.data.href);
    });
    syncLocation(window.location.href);
    window.postMessage({ source: PAGE_SOURCE, type: "request-location" }, "*");
    storageGet().then((stored) => {
      Object.assign(config, stored);
      configReady = true;
      renderPanel();
      notify();
      scheduleAutoSearch();
    });
  }

  start();
})();
