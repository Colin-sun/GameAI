"use strict";

importScripts("engine.js", "model.js", "search-runtime.js");

const DEFAULT_CONFIG = Object.freeze({
  mode: "tactical",
  autoPlay: true,
  aiPlayer: 2,
  playouts: 256,
  cPuct: 0.8,
  rolloutLimit: 32,
  seed: 20260811,
});

chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.local.get("config").then((stored) => {
    if (!stored.config) {
      return chrome.storage.local.set({ config: { ...DEFAULT_CONFIG } });
    }
    return undefined;
  }).catch(() => {
    // The content script still has its own defaults if storage is unavailable.
  });
});

chrome.commands.onCommand.addListener(async (command) => {
  if (command !== "search-suggestion") return;
  const tabs = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
  const tab = tabs[0];
  if (!tab || tab.id == null) return;
  chrome.tabs.sendMessage(tab.id, { type: "search", origin: "shortcut" }).catch(() => {});
});

const jobs = new Map();

function jobKey(tabId, id) {
  return `${tabId}:${id}`;
}

function sendToTab(tabId, message) {
  chrome.tabs.sendMessage(tabId, message, () => {
    void chrome.runtime.lastError;
  });
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || !sender.tab || sender.tab.id == null) return false;
  const tabId = sender.tab.id;
  if (message.type === "cancelSearch") {
    const job = jobs.get(jobKey(tabId, message.id));
    if (job) job.cancelled = true;
    sendResponse({ accepted: true });
    return false;
  }
  if (message.type !== "searchRequest" || !message.request || message.request.id == null) {
    return false;
  }
  const request = message.request;
  const key = jobKey(tabId, request.id);
  for (const [existingKey, job] of jobs) {
    if (job.tabId === tabId) {
      job.cancelled = true;
      jobs.delete(existingKey);
    }
  }
  const job = { tabId, cancelled: false };
  jobs.set(key, job);
  sendResponse({ accepted: true });
  self.GameAISearchRuntime.runSearch(
    request,
    (progress) => sendToTab(tabId, { type: "aiWorker", message: progress }),
    () => job.cancelled,
  ).then((result) => {
    if (!job.cancelled) {
      sendToTab(tabId, { type: "aiWorker", message: { type: "result", id: request.id, result } });
    }
  }).catch((error) => {
    if (!job.cancelled) {
      sendToTab(tabId, { type: "aiWorker", message: {
        type: "error",
        id: request.id,
        error: error && error.message ? error.message : String(error),
      } });
    }
  }).finally(() => {
    if (jobs.get(key) === job) jobs.delete(key);
  });
  return false;
});
