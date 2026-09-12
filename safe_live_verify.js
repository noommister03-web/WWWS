"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs/promises");
const path = require("node:path");
const { DatabaseSync } = require("node:sqlite");
const { chromium } = require("playwright");

const WORKER = "http://127.0.0.1:3001";
const SECRET = String(process.env.BROWSER_WORKER_SHARED_SECRET || "");
const DB_PATH = String(process.env.DB_PATH || "/app/data/bot.sqlite3");
const MANUAL_ACCOUNT_ID = String(process.env.CJ_MANUAL_ACCOUNT_ID || "").trim();
const PROFILE_ROOT = String(process.env.CJ_PROFILE_ROOT || "/app/data/custojusto/profiles");
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

function sanitizeEvidence(value) {
  const evidence = value && typeof value === "object" ? value : {};
  const routes = new Set(["login", "messages", "other", "invalid"]);
  const route = (key) => routes.has(evidence[key]) ? evidence[key] : "invalid";
  return {
    routeBefore: route("routeBefore"),
    routeAfter: route("routeAfter"),
    messageProbe: Boolean(evidence.messageProbe),
    loginFormVisible: Boolean(evidence.loginFormVisible),
    loginActionVisible: Boolean(evidence.loginActionVisible),
    challengeVisible: Boolean(evidence.challengeVisible),
    authMarkerCount: Math.max(0, Math.min(20, Number(evidence.authMarkerCount) || 0)),
  };
}

function routeKind(value) {
  try {
    const pathname = new URL(value).pathname.toLowerCase();
    if (/(?:^|\/)(?:login|entrar|signin)(?:\/|$)/.test(pathname)) return "login";
    if (/(?:^|\/)(?:mensagens|messages)(?:\/|$)/.test(pathname)) return "messages";
    return "other";
  } catch (_) {
    return "invalid";
  }
}

async function anyVisible(page, selector) {
  const values = page.locator(selector);
  const count = await values.count().catch(() => 0);
  for (let index = 0; index < count; index += 1) {
    if (await values.nth(index).isVisible().catch(() => false)) return true;
  }
  return false;
}

async function inspectCopiedProfile(accountId, baseUrl) {
  let context = null;
  let present = false;
  let phase = "profile_check";
  const tempRoot = await fs.mkdtemp("/tmp/cj-profile-probe-");
  const source = path.join(PROFILE_ROOT, String(accountId));
  const profile = path.join(tempRoot, "profile");
  try {
    const stat = await fs.stat(source).catch(() => null);
    if (!stat || !stat.isDirectory()) {
      return { accountId, present: false, loggedIn: false, error: "profile_missing" };
    }
    present = true;
    phase = "profile_copy";
    await fs.cp(source, profile, { recursive: true, force: true });
    for (const parent of [profile, path.join(profile, "Default")]) {
      for (const name of ["SingletonLock", "SingletonCookie", "SingletonSocket"]) {
        await fs.rm(path.join(parent, name), { force: true, recursive: true }).catch(() => {});
      }
    }

    phase = "browser_launch";
    context = await chromium.launchPersistentContext(profile, {
      headless: false,
      viewport: { width: 1365, height: 900 },
      timeout: 120000,
      args: ["--no-sandbox", "--disable-dev-shm-usage", "--start-maximized"],
    });
    const page = context.pages()[0] || await context.newPage();
    page.setDefaultTimeout(60000);

    phase = "messages_navigation";
    let navigationFailed = false;
    await page.goto(new URL("/mensagens", baseUrl).toString(), {
      waitUntil: "domcontentloaded",
      timeout: 60000,
    }).catch(() => { navigationFailed = true; });
    await page.waitForTimeout(1500);

    const route = routeKind(page.url());
    const loginFormVisible = await anyVisible(page, 'input[type="password"],input#password,form[action*="login" i]');
    const loginActionVisible = await anyVisible(page, 'a[href*="login" i],a[href*="entrar" i],button:has-text("Entrar"),button:has-text("Iniciar sessão")');
    const challengeVisible = await anyVisible(page, 'iframe[src*="challenges.cloudflare.com"],iframe[title*="challenge" i],#challenge-running,.cf-challenge');
    const authMarkerCount = Math.min(20, await page.locator('a[href*="conta" i],a[href*="account" i],a[href*="mensagens" i],a[href*="messages" i],a[href*="logout" i],a[href*="sair" i],[data-testid*="account" i],[aria-label*="conta" i]').count().catch(() => 0));
    const loggedIn = !navigationFailed && route === "messages" && !loginFormVisible && !challengeVisible && (!loginActionVisible || authMarkerCount > 0);
    return {
      accountId,
      present,
      loggedIn,
      route,
      navigationFailed,
      loginFormVisible,
      loginActionVisible,
      challengeVisible,
      authMarkerCount,
    };
  } catch (_) {
    return { accountId, present, loggedIn: false, error: `${phase}_failed` };
  } finally {
    if (context) await context.close().catch(() => {});
    await fs.rm(tempRoot, { recursive: true, force: true }).catch(() => {});
  }
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
  let accounts;
  let drafts;
  try {
    accounts = db.prepare(`
      SELECT id, login_url
      FROM custojusto_accounts
      ORDER BY id ASC
      LIMIT 50
    `).all();
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

  const databaseAccountIds = new Set(accounts.map((account) => Number(account.id)));
  const sessionTargets = accounts.map((account) => ({ ...account, manualConfigured: String(account.id) === MANUAL_ACCOUNT_ID }));
  if (/^[1-9]\d{0,15}$/.test(MANUAL_ACCOUNT_ID)) {
    const manualAccountId = Number(MANUAL_ACCOUNT_ID);
    if (Number.isSafeInteger(manualAccountId) && !databaseAccountIds.has(manualAccountId)) {
      sessionTargets.push({
        id: manualAccountId,
        login_url: "https://www.custojusto.pt",
        manualConfigured: true,
      });
    }
  }

  const profileSessions = [];
  for (const account of sessionTargets) {
    profileSessions.push(await inspectCopiedProfile(Number(account.id), custoJustoOrigin(account.login_url)));
  }

  const sessions = new Map();
  const evidenceByAccount = new Map();
  const sessionErrors = [];
  const results = [];
  let requestFailures = 0;

  for (const account of sessionTargets) {
    const accountId = Number(account.id);
    try {
      const status = await post("/status", { accountId, baseUrl: custoJustoOrigin(account.login_url) });
      sessions.set(accountId, Boolean(status && status.loggedIn));
      evidenceByAccount.set(accountId, sanitizeEvidence(status && status.evidence));
    } catch (error) {
      sessions.set(accountId, false);
      requestFailures += 1;
      sessionErrors.push({
        accountId,
        error: String(error.code || error.message || "status_failed"),
      });
    }
  }

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

  const sessionSummary = [...sessions.entries()].map(([accountId, loggedIn]) => ({
    accountId,
    loggedIn,
    inDatabase: databaseAccountIds.has(accountId),
    manualConfigured: String(accountId) === MANUAL_ACCOUNT_ID,
    evidence: evidenceByAccount.get(accountId) || null,
  }));
  const summary = {
    ok: requestFailures === 0 && sessionSummary.every((item) => item.loggedIn),
    mode: "status-and-messages-only",
    noSend: true,
    databaseReadOnly: true,
    draftCount: drafts.length,
    requestFailures,
    sessions: sessionSummary,
    profileSessions,
    sessionErrors,
    results,
  };
  console.log(`[safe-live-verify] ${JSON.stringify(summary)}`);
  if (!summary.ok) process.exitCode = 1;
})().catch((error) => {
  console.error(`[safe-live-verify] ${JSON.stringify({ ok: false, noSend: true, databaseReadOnly: true, fatal: String(error.code || error.message || "verification_failed") })}`);
  process.exitCode = 1;
});
