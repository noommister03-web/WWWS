"use strict";

const crypto = require("node:crypto");
const { DatabaseSync } = require("node:sqlite");

const WORKER = "http://127.0.0.1:3001";
const SECRET = String(process.env.BROWSER_WORKER_SHARED_SECRET || "");
const DB_PATH = String(process.env.DB_PATH || "/app/data/bot.sqlite3");
const REQUEST_TIMEOUT_MS = 300000;

function normalize(value) {
  return String(value || "").replace(/\u00a0/g, " ").trim().replace(/\s+/g, " ");
}

function fingerprint(value) {
  return crypto.createHash("sha256").update(String(value || "")).digest("hex").slice(0, 16);
}

function custoJustoOrigin(value) {
  const url = new URL(String(value || "https://www.custojusto.pt"));
  const host = url.hostname.toLowerCase().replace(/\.$/, "");
  if (url.protocol !== "https:" || !(host === "custojusto.pt" || host.endsWith(".custojusto.pt")) || url.username || url.password || (url.port && url.port !== "443")) {
    throw new Error("invalid_custojusto_url");
  }
  return url.origin;
}

async function post(endpoint, body) {
  const response = await fetch(`${WORKER}${endpoint}`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-worker-secret": SECRET,
    },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });
  if (!response.ok) {
    const error = new Error(`worker_http_${response.status}`);
    error.code = `worker_http_${response.status}`;
    throw error;
  }
  return response.json();
}

(async () => {
  if (!SECRET) throw new Error("worker_secret_missing");

  const db = new DatabaseSync(DB_PATH, { readOnly: true });
  let drafts;
  try {
    drafts = db.prepare(`
      SELECT d.id, d.account_id, COALESCE(d.conversation_id, 0) AS conversation_id,
             d.target_url, d.text, d.status, a.login_url
      FROM custojusto_drafts AS d
      JOIN custojusto_accounts AS a ON a.id = d.account_id
      ORDER BY d.id ASC
      LIMIT 50
    `).all();
  } finally {
    db.close();
  }

  const sessions = new Map();
  const results = [];
  let requestFailures = 0;

  for (const draft of drafts) {
    const accountId = Number(draft.account_id);
    const expected = normalize(draft.text);
    const baseUrl = custoJustoOrigin(draft.login_url);
    custoJustoOrigin(draft.target_url);

    if (!sessions.has(accountId)) {
      try {
        const status = await post("/status", { accountId, baseUrl });
        sessions.set(accountId, Boolean(status && status.loggedIn));
      } catch (error) {
        sessions.set(accountId, false);
        requestFailures += 1;
        results.push({
          draftId: Number(draft.id),
          draftStatus: String(draft.status),
          conversationId: Number(draft.conversation_id),
          textFp: fingerprint(expected),
          targetFp: fingerprint(draft.target_url),
          error: String(error.code || error.message || "status_failed"),
        });
        continue;
      }
    }

    if (!sessions.get(accountId)) {
      results.push({
        draftId: Number(draft.id),
        draftStatus: String(draft.status),
        conversationId: Number(draft.conversation_id),
        textFp: fingerprint(expected),
        targetFp: fingerprint(draft.target_url),
        error: "session_not_logged_in",
      });
      continue;
    }

    try {
      const messages = await post("/messages", {
        accountId,
        conversationUrl: String(draft.target_url),
        fullHistory: false,
      });
      if (!Array.isArray(messages)) throw new Error("messages_not_array");

      const normalized = messages.map((message) => ({
        text: normalize(message && message.text),
        outgoing: Boolean(message && message.incoming === false),
      }));
      const matching = normalized.filter((message) => expected.length > 0 && message.text.includes(expected));

      results.push({
        draftId: Number(draft.id),
        draftStatus: String(draft.status),
        conversationId: Number(draft.conversation_id),
        textFp: fingerprint(expected),
        targetFp: fingerprint(draft.target_url),
        messageCount: normalized.length,
        outgoingCount: normalized.filter((message) => message.outgoing).length,
        foundAny: matching.length > 0,
        foundOutgoing: matching.some((message) => message.outgoing),
      });
    } catch (error) {
      requestFailures += 1;
      results.push({
        draftId: Number(draft.id),
        draftStatus: String(draft.status),
        conversationId: Number(draft.conversation_id),
        textFp: fingerprint(expected),
        targetFp: fingerprint(draft.target_url),
        error: String(error.code || error.message || "messages_failed"),
      });
    }
  }

  const sessionSummary = [...sessions.entries()].map(([accountId, loggedIn]) => ({ accountId, loggedIn }));
  const summary = {
    ok: requestFailures === 0 && sessionSummary.every((item) => item.loggedIn),
    mode: "status-and-messages-only",
    noSend: true,
    databaseReadOnly: true,
    draftCount: drafts.length,
    requestFailures,
    sessions: sessionSummary,
    results,
  };
  console.log(`[safe-live-verify] ${JSON.stringify(summary)}`);
  if (!summary.ok) process.exitCode = 1;
})().catch((error) => {
  console.error(`[safe-live-verify] ${JSON.stringify({ ok: false, noSend: true, databaseReadOnly: true, fatal: String(error.code || error.message || "verification_failed") })}`);
  process.exitCode = 1;
});
