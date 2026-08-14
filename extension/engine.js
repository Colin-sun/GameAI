(function (root) {
  "use strict";

  const BOARD_SIZE = 9;
  const ACTION_SIZE = 81;
  const RULE_VERSION = "majority-utt-v1";
  const LOCAL_LINES = [
    [0, 1, 2],
    [3, 4, 5],
    [6, 7, 8],
    [0, 3, 6],
    [1, 4, 7],
    [2, 5, 8],
    [0, 4, 8],
    [2, 4, 6],
  ];

  function siteActionToNative(action) {
    const siteRow = Math.floor(action / BOARD_SIZE);
    const col = action % BOARD_SIZE;
    return (BOARD_SIZE - 1 - siteRow) * BOARD_SIZE + col;
  }

  function nativeActionToSite(action) {
    const nativeRow = Math.floor(action / BOARD_SIZE);
    const col = action % BOARD_SIZE;
    return (BOARD_SIZE - 1 - nativeRow) * BOARD_SIZE + col;
  }

  function siteActionLabel(action) {
    if (!Number.isInteger(action) || action < 0 || action >= ACTION_SIZE) {
      return "--";
    }
    return String.fromCharCode(97 + (action % BOARD_SIZE)) +
      String(1 + Math.floor(action / BOARD_SIZE));
  }

  function parseSiteMoves(value) {
    if (typeof value !== "string") {
      return { ok: false, error: "The page has no p move sequence." };
    }
    if (value.length % 2 !== 0) {
      return { ok: false, error: "The p move sequence is incomplete." };
    }

    const actions = [];
    const seen = new Set();
    for (let index = 0; index < value.length; index += 2) {
      const col = value.charCodeAt(index) - 97;
      const row = value.charCodeAt(index + 1) - 49;
      if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) {
        return { ok: false, error: `Invalid move ${value.slice(index, index + 2)}.` };
      }
      const action = row * BOARD_SIZE + col;
      if (seen.has(action)) {
        return { ok: false, error: `Move ${siteActionLabel(action)} is repeated.` };
      }
      seen.add(action);
      actions.push(action);
    }
    return { ok: true, actions };
  }

  function formatSiteMoves(actions) {
    return actions.map(siteActionLabel).join("");
  }

  function localBoardState(board, metaRow, metaCol, overrideIndex, overridePlayer) {
    const values = new Uint8Array(9);
    const baseRow = metaRow * 3;
    const baseCol = metaCol * 3;
    for (let row = 0; row < 3; row += 1) {
      for (let col = 0; col < 3; col += 1) {
        const localIndex = row * 3 + col;
        const action = (baseRow + row) * BOARD_SIZE + baseCol + col;
        values[localIndex] = action === overrideIndex ? overridePlayer : board[action];
      }
    }
    for (const line of LOCAL_LINES) {
      const first = values[line[0]];
      if (first !== 0 && first === values[line[1]] && first === values[line[2]]) {
        return first;
      }
    }
    for (const value of values) {
      if (value === 0) {
        return 0;
      }
    }
    return 3;
  }

  function majorityWinner(metaBoard) {
    let playerOneWins = 0;
    let playerTwoWins = 0;
    let draws = 0;
    let resolved = 0;
    for (const state of metaBoard) {
      playerOneWins += state === 1 ? 1 : 0;
      playerTwoWins += state === 2 ? 1 : 0;
      draws += state === 3 ? 1 : 0;
      resolved += state !== 0 ? 1 : 0;
    }
    const threshold = Math.floor((9 - draws) / 2) + 1;
    if (playerOneWins >= threshold) {
      return 1;
    }
    if (playerTwoWins >= threshold) {
      return 2;
    }
    if (resolved === 9) {
      if (playerOneWins > playerTwoWins) {
        return 1;
      }
      if (playerTwoWins > playerOneWins) {
        return 2;
      }
      return -1;
    }
    return 0;
  }

  class GameState {
    constructor() {
      this.board = new Uint8Array(ACTION_SIZE);
      this.metaBoard = new Uint8Array(9);
      this.nextBoardRow = -1;
      this.nextBoardCol = -1;
      this.currentPlayer = 1;
      this.step = 0;
    }

    static fromSiteMoves(siteActions) {
      const state = new GameState();
      for (let index = 0; index < siteActions.length; index += 1) {
        const siteAction = siteActions[index];
        const nativeAction = siteActionToNative(siteAction);
        if (!state.makeMove(nativeAction)) {
          throw new Error(
            `Move ${siteActionLabel(siteAction)} is not legal under ${RULE_VERSION} at step ${index}.`,
          );
        }
      }
      return state;
    }

    clone() {
      const copy = new GameState();
      copy.board.set(this.board);
      copy.metaBoard.set(this.metaBoard);
      copy.nextBoardRow = this.nextBoardRow;
      copy.nextBoardCol = this.nextBoardCol;
      copy.currentPlayer = this.currentPlayer;
      copy.step = this.step;
      return copy;
    }

    getDoneWinner() {
      const winner = majorityWinner(this.metaBoard);
      return { done: winner !== 0, winner };
    }

    getValidActions() {
      if (this.getDoneWinner().done) {
        return [];
      }
      const actions = [];
      if (this.nextBoardRow >= 0 && this.nextBoardCol >= 0 &&
          this.metaBoard[this.nextBoardRow * 3 + this.nextBoardCol] === 0) {
        const baseRow = this.nextBoardRow * 3;
        const baseCol = this.nextBoardCol * 3;
        for (let row = 0; row < 3; row += 1) {
          for (let col = 0; col < 3; col += 1) {
            const action = (baseRow + row) * BOARD_SIZE + baseCol + col;
            if (this.board[action] === 0) {
              actions.push(action);
            }
          }
        }
        return actions;
      }
      for (let action = 0; action < ACTION_SIZE; action += 1) {
        if (this.board[action] === 0) {
          const metaRow = Math.floor(action / 27);
          const metaCol = Math.floor((action % 9) / 3);
          if (this.metaBoard[metaRow * 3 + metaCol] === 0) {
            actions.push(action);
          }
        }
      }
      return actions;
    }

    isActionValid(action) {
      return this.getValidActions().includes(action);
    }

    makeMove(action) {
      if (!Number.isInteger(action) || action < 0 || action >= ACTION_SIZE ||
          !this.isActionValid(action)) {
        return false;
      }
      const row = Math.floor(action / BOARD_SIZE);
      const col = action % BOARD_SIZE;
      const metaRow = Math.floor(row / 3);
      const metaCol = Math.floor(col / 3);
      this.board[action] = this.currentPlayer;
      this.metaBoard[metaRow * 3 + metaCol] = localBoardState(
        this.board,
        metaRow,
        metaCol,
        -1,
        0,
      );

      const targetRow = row % 3;
      const targetCol = col % 3;
      if (this.metaBoard[targetRow * 3 + targetCol] === 0) {
        this.nextBoardRow = targetRow;
        this.nextBoardCol = targetCol;
      } else {
        this.nextBoardRow = -1;
        this.nextBoardCol = -1;
      }
      this.currentPlayer = 3 - this.currentPlayer;
      this.step += 1;
      return true;
    }

    completesLocalBoard(action, player) {
      if (!this.isActionValid(action) || (player !== 1 && player !== 2)) {
        return false;
      }
      const row = Math.floor(action / BOARD_SIZE);
      const col = action % BOARD_SIZE;
      return localBoardState(
        this.board,
        Math.floor(row / 3),
        Math.floor(col / 3),
        action,
        player,
      ) === player;
    }

    winsGameWithAction(action, player) {
      if (!this.completesLocalBoard(action, player)) {
        return false;
      }
      const row = Math.floor(action / BOARD_SIZE);
      const col = action % BOARD_SIZE;
      const nextMeta = new Uint8Array(this.metaBoard);
      nextMeta[Math.floor(row / 3) * 3 + Math.floor(col / 3)] = player;
      return majorityWinner(nextMeta) === player;
    }

    selectRolloutAction(actions, random, rolloutPolicy) {
      if (rolloutPolicy === 0) {
        return actions[random.nextInt(actions.length)];
      }
      const player = this.currentPlayer;
      const opponent = 3 - player;
      const priorities = actions.map((action) => {
        if (this.winsGameWithAction(action, player)) return 4;
        if (this.winsGameWithAction(action, opponent)) return 3;
        if (this.completesLocalBoard(action, player)) return 2;
        if (this.completesLocalBoard(action, opponent)) return 1;
        return 0;
      });
      const best = Math.max(...priorities);
      const candidates = actions.filter((_action, index) => priorities[index] === best);
      return candidates[random.nextInt(candidates.length)];
    }

    evaluate(player) {
      if (player !== 1 && player !== 2) {
        return 0;
      }
      const result = this.getDoneWinner();
      if (result.done) {
        if (result.winner === -1) return 0;
        return result.winner === player ? 1 : -1;
      }
      const opponent = 3 - player;
      let score = 0.35 * (
        this.metaBoard.filter((value) => value === player).length -
        this.metaBoard.filter((value) => value === opponent).length
      );
      for (let metaRow = 0; metaRow < 3; metaRow += 1) {
        for (let metaCol = 0; metaCol < 3; metaCol += 1) {
          if (this.metaBoard[metaRow * 3 + metaCol] !== 0) continue;
          const values = new Uint8Array(9);
          for (let row = 0; row < 3; row += 1) {
            for (let col = 0; col < 3; col += 1) {
              values[row * 3 + col] = this.board[
                (metaRow * 3 + row) * BOARD_SIZE + metaCol * 3 + col
              ];
            }
          }
          for (const line of LOCAL_LINES) {
            let own = 0;
            let theirs = 0;
            for (const index of line) {
              own += values[index] === player ? 1 : 0;
              theirs += values[index] === opponent ? 1 : 0;
            }
            if (theirs === 0 && own > 0) score += own === 2 ? 0.055 : 0.012;
            else if (own === 0 && theirs > 0) score -= theirs === 2 ? 0.055 : 0.012;
          }
        }
      }
      return Math.tanh(score);
    }
  }

  class RandomSource {
    constructor(seed) {
      this.value = (Number(seed) >>> 0) || 0x6d2b79f5;
    }

    nextUint32() {
      let value = this.value;
      value ^= value << 13;
      value ^= value >>> 17;
      value ^= value << 5;
      this.value = value >>> 0;
      return this.value;
    }

    nextFloat() {
      return this.nextUint32() / 0x100000000;
    }

    nextInt(length) {
      return Math.floor(this.nextFloat() * length);
    }
  }

  class SearchNode {
    constructor(prior = 1) {
      this.prior = Number(prior);
      this.visits = 0;
      this.valueSum = 0;
      this.children = new Map();
    }

    get q() {
      return this.visits ? this.valueSum / this.visits : 0;
    }
  }

  function uniformPriors(actions) {
    const priors = new Map();
    const value = 1 / actions.length;
    for (const action of actions) priors.set(action, value);
    return priors;
  }

  function maskedPolicy(policy, actions) {
    const priors = new Map();
    let total = 0;
    for (const action of actions) {
      const value = Number.isFinite(policy[action]) ? Math.max(0, policy[action]) : 0;
      priors.set(action, value);
      total += value;
    }
    if (!(total > 0) || !Number.isFinite(total)) {
      return uniformPriors(actions);
    }
    for (const action of actions) priors.set(action, priors.get(action) / total);
    return priors;
  }

  function encodeState(game) {
    const planes = [];
    const player = game.currentPlayer;
    const opponent = 3 - player;
    const stonePlane = (value) => {
      const plane = new Float32Array(ACTION_SIZE);
      for (let index = 0; index < ACTION_SIZE; index += 1) {
        plane[index] = game.board[index] === value ? 1 : 0;
      }
      return plane;
    };
    planes.push(stonePlane(player), stonePlane(opponent));

    for (const metaValue of [player, opponent, 3, 0]) {
      const plane = new Float32Array(ACTION_SIZE);
      for (let metaRow = 0; metaRow < 3; metaRow += 1) {
        for (let metaCol = 0; metaCol < 3; metaCol += 1) {
          const value = game.metaBoard[metaRow * 3 + metaCol] === metaValue ? 1 : 0;
          for (let row = 0; row < 3; row += 1) {
            for (let col = 0; col < 3; col += 1) {
              plane[(metaRow * 3 + row) * BOARD_SIZE + metaCol * 3 + col] = value;
            }
          }
        }
      }
      planes.push(plane);
    }

    const target = new Float32Array(ACTION_SIZE);
    const free = new Float32Array(ACTION_SIZE);
    if (game.nextBoardRow < 0) {
      free.fill(1);
    } else {
      for (let row = game.nextBoardRow * 3; row < game.nextBoardRow * 3 + 3; row += 1) {
        for (let col = game.nextBoardCol * 3; col < game.nextBoardCol * 3 + 3; col += 1) {
          target[row * BOARD_SIZE + col] = 1;
        }
      }
    }
    planes.push(target, free);

    const playerPlane = new Float32Array(ACTION_SIZE);
    playerPlane.fill(player === 1 ? 1 : 0);
    const stepPlane = new Float32Array(ACTION_SIZE);
    stepPlane.fill(game.step / ACTION_SIZE);
    planes.push(playerPlane, stepPlane);

    const encoded = new Float32Array(10 * ACTION_SIZE);
    for (let plane = 0; plane < planes.length; plane += 1) {
      encoded.set(planes[plane], plane * ACTION_SIZE);
    }
    return encoded;
  }

  class MCTS {
    constructor(options = {}) {
      this.mode = options.mode || "tactical";
      this.playouts = Math.max(1, Math.floor(options.playouts || 512));
      this.cPuct = Number(options.cPuct || 0.3);
      this.rolloutLimit = Math.max(1, Math.floor(options.rolloutLimit || 32));
      this.rootSelection = options.rootSelection || (this.mode === "prior" ? "q" : "visits");
      this.random = new RandomSource(options.seed);
      this.root = new SearchNode();
    }

    expand(node, actions, priors) {
      for (const action of actions) {
        node.children.set(action, new SearchNode(priors.get(action) || 0));
      }
    }

    selectChild(node) {
      const parentVisits = Math.sqrt(node.visits);
      let bestAction = -1;
      let bestChild = null;
      let bestScore = -Infinity;
      for (const [action, child] of node.children) {
        const score = child.q + this.cPuct * child.prior * parentVisits / (1 + child.visits);
        if (score > bestScore || (score === bestScore && action < bestAction)) {
          bestScore = score;
          bestAction = action;
          bestChild = child;
        }
      }
      return [bestAction, bestChild];
    }

    rolloutValue(state) {
      const perspective = state.currentPlayer;
      for (let index = 0; index < this.rolloutLimit; index += 1) {
        const result = state.getDoneWinner();
        if (result.done) {
          if (result.winner === -1) return 0;
          return result.winner === perspective ? 1 : -1;
        }
        const actions = state.getValidActions();
        if (!actions.length) throw new Error("Non-terminal state has no rollout action.");
        const policy = this.mode === "pure" ? 0 : 1;
        const action = state.selectRolloutAction(actions, this.random, policy);
        if (!state.makeMove(action)) throw new Error("Rollout selected an invalid action.");
      }
      const result = state.getDoneWinner();
      if (result.done) {
        if (result.winner === -1) return 0;
        return result.winner === perspective ? 1 : -1;
      }
      return Math.max(-1, Math.min(1, state.evaluate(perspective)));
    }

    playout(game) {
      const state = game.clone();
      let node = this.root;
      const path = [node];
      while (node.children.size) {
        const [action, child] = this.selectChild(node);
        if (!child || !state.makeMove(action)) throw new Error("Search selected an invalid action.");
        node = child;
        path.push(node);
      }
      const result = state.getDoneWinner();
      if (!result.done) {
        const actions = state.getValidActions();
        this.expand(node, actions, uniformPriors(actions));
      }
      const leafValue = this.rolloutValue(state);
      let value = -leafValue;
      for (let index = path.length - 1; index >= 0; index -= 1) {
        const current = path[index];
        current.visits += 1;
        current.valueSum += value;
        value = -value;
      }
    }

    rootPriors(game, model) {
      const actions = game.getValidActions();
      if (this.mode !== "prior") return uniformPriors(actions);
      if (!model) throw new Error("The prior model is not loaded.");
      const prediction = model.predict(encodeState(game));
      return maskedPolicy(prediction.policy, actions);
    }

    chooseAction() {
      let bestAction = -1;
      let bestNode = null;
      for (const [action, child] of this.root.children) {
        if (!bestNode ||
            (this.rootSelection === "q" && child.q > bestNode.q) ||
            (this.rootSelection === "q" && child.q === bestNode.q && child.visits > bestNode.visits) ||
            (this.rootSelection !== "q" && child.visits > bestNode.visits) ||
            ((this.rootSelection === "q" ? child.q === bestNode.q : child.visits === bestNode.visits) &&
             action < bestAction)) {
          bestAction = action;
          bestNode = child;
        }
      }
      return bestAction;
    }

    async search(game, model, hooks = {}) {
      const result = game.getDoneWinner();
      if (result.done) return { action: -1, rootVisits: 0, nodeCount: 1, winner: result.winner };
      const actions = game.getValidActions();
      if (!actions.length) return { action: -1, rootVisits: 0, nodeCount: 1, winner: 0 };

      if (this.mode === "prior") {
        this.expand(this.root, actions, this.rootPriors(game, model));
      }
      const chunkSize = Math.max(1, Math.min(32, Math.floor(hooks.chunkSize || 16)));
      for (let index = 0; index < this.playouts; index += 1) {
        if (hooks.isCancelled && hooks.isCancelled()) {
          throw new Error("SEARCH_CANCELLED");
        }
        this.playout(game);
        if ((index + 1) % chunkSize === 0 || index + 1 === this.playouts) {
          if (hooks.onProgress) hooks.onProgress(index + 1, this.playouts);
          await new Promise((resolve) => setTimeout(resolve, 0));
        }
      }
      const action = this.chooseAction();
      let nodeCount = 1;
      for (const child of this.root.children.values()) {
        nodeCount += 1 + child.children.size;
      }
      return {
        action,
        rootVisits: this.root.visits,
        nodeCount,
        winner: 0,
        q: action >= 0 ? this.root.children.get(action).q : 0,
      };
    }
  }

  const api = {
    ACTION_SIZE,
    BOARD_SIZE,
    RULE_VERSION,
    GameState,
    MCTS,
    RandomSource,
    encodeState,
    formatSiteMoves,
    majorityWinner,
    nativeActionToSite,
    parseSiteMoves,
    siteActionLabel,
    siteActionToNative,
  };

  root.GameAIEngine = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis === "undefined" ? this : globalThis);
