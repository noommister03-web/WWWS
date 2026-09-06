const express = require("express");
const { chromium } = require("playwright");
const crypto = require("crypto");
const fs = require("fs");
const fsp = require("fs/promises");
const path = require("path");

const app = express();
app.use(express.json({limit: "1mb"}));

// Railway sets PORT for public web services. This worker is private to the bot container.
const PORT = Number(process.env.BROWSER_WORKER_PORT || 3001);
const WORKER_SHARED_SECRET = process.env.BROWSER_WORKER_SHARED_SECRET || "";
const BASE = process.env.CJ_BASE_URL || "https://www.custojusto.pt";
const PROFILE_ROOT = process.env.CJ_PROFILE_ROOT || path.join(process.cwd(), "data", "custojusto", "profiles");

function safeAccountId(value) {
  const id = String(value || "").trim();
  if (!/^[A-Za-z0-9_-]{1,100}$/.test(id)) throw new Error("Invalid account id");
  return id;
}

function accountProfileDir(accountId) {
  return path.join(PROFILE_ROOT, safeAccountId(accountId));
}

function requireAuth(req, res, next) {
  if (!WORKER_SHARED_SECRET) return res.status(503).json({error: "BROWSER_WORKER_SHARED_SECRET is not configured"});
  const supplied = req.get("x-worker-secret") || "";
  const expected = Buffer.from(WORKER_SHARED_SECRET);
  const actual = Buffer.from(supplied);
  if (actual.length !== expected.length || !crypto.timingSafeEqual(actual, expected)) {
    return res.status(401).json({error: "Unauthorized"});
  }
  next();
}

function toAbsoluteUrl(href) {
  if (!href) return "";
  try { return new URL(href, BASE).toString(); } catch { return ""; }
}

async function openPage(accountId) {
  const profileDir = accountProfileDir(accountId);
  await fsp.mkdir(profileDir, {recursive: true});
  const context = await chromium.launchPersistentContext(profileDir, {
    headless: process.env.CJ_HEADLESS !== "false",
    viewport: {width: 1365, height: 900},
    args: ["--no-sandbox", "--disable-dev-shm-usage"]
  });
  const pages = context.pages();
  const page = pages[0] || await context.newPage();
  page.setDefaultTimeout(Number(process.env.CJ_ACTION_TIMEOUT_MS || 30000));
  return {context, page};
}

async function closeContext(context) {
  try { await context.close(); } catch (_) {}
}

async function isLoggedIn(page) {
  const url = page.url();
  if (/login|entrar|signin/i.test(url)) return false;
  const loggedOut = await page.locator('a[href*="login"], a[href*="entrar"], button:has-text("Entrar")').count();
  const accountUi = await page.locator('a[href*="conta"], a[href*="account"], a[href*="mensagens"], a[href*="messages"]').count();
  return accountUi > 0 && loggedOut === 0;
}

async function login(page, email, password) {
  await page.goto(BASE, {waitUntil: "domcontentloaded"});
  if (await isLoggedIn(page)) return {ok: true, alreadyLoggedIn: true};

  const selectors = [
    'a[href*="login"]', 'a[href*="entrar"]', 'button:has-text("Entrar")', 'text=/entrar|login/i'
  ];
  for (const selector of selectors) {
    const candidate = page.locator(selector).first();
    if (await candidate.count()) {
      try { await candidate.click(); await page.waitForTimeout(700); break; } catch (_) {}
    }
  }

  const emailField = page.locator('input[type="email"], input[name*="email" i], input[id*="email" i]').first();
  const passwordField = page.locator('input[type="password"]').first();
  await emailField.fill(email);
  await passwordField.fill(password);
  const submit = page.locator('button[type="submit"], input[type="submit"], button:has-text("Entrar"), button:has-text("Login")').first();
  await submit.click();
  await page.waitForTimeout(1500);

  if (await isLoggedIn(page)) return {ok: true};
  const url = page.url();
  const body = (await page.locator("body").innerText().catch(() => "")).slice(0, 1000);
  return {ok: false, error: "Login was not confirmed", url, pageText: body};
}

async function findConversationOrListing(page, listingUrl) {
  await page.goto(listingUrl, {waitUntil: "domcontentloaded"});
  const candidates = [
    'a:has-text("Contactar")', 'button:has-text("Contactar")',
    'a:has-text("Mensagem")', 'button:has-text("Mensagem")',
    'a[href*="mensagens"]', 'a[href*="messages"]'
  ];
  for (const selector of candidates) {
    const el = page.locator(selector).first();
    if (await el.count()) {
      await el.click();
      await page.waitForTimeout(700);
      return;
    }
  }
  throw new Error("Could not find a contact/message action on this listing");
}

async function sendMessage(page, listingUrl, message) {
  await findConversationOrListing(page, listingUrl);
  const messageBox = page.locator('textarea, [contenteditable="true"], input[name*="message" i], textarea[name*="message" i]').first();
  await messageBox.fill(message);
  const send = page.locator('button[type="submit"], button:has-text("Enviar"), button:has-text("Send")').first();
  await send.click();
  await page.waitForTimeout(500);
  return {ok: true, url: page.url()};
}

async function extractMessages(page) {
  const rows = await page.locator('a[href*="mensagens"], a[href*="messages"], a[href*="conversa"], a[href*="conversation"]').evaluateAll(nodes => nodes.map((node, index) => {
    const href = node.getAttribute("href") || "";
    const text = (node.innerText || node.textContent || "").trim().replace(/\s+/g, " ");
    return {index, href, text};
  }).filter(x => x.href || x.text));
  const unique = new Map();
  for (const row of rows) {
    const key = row.href || row.text;
    if (key && !unique.has(key)) unique.set(key, row);
  }
  return [...unique.values()].slice(0, 100).map(row => ({...row, href: toAbsoluteUrl(row.href)}));
}

async function collectConversation(page, conversationUrl) {
  await page.goto(conversationUrl, {waitUntil: "domcontentloaded"});
  const messages = await page.locator('article, [data-message-id], .message, [class*="message"]').evaluateAll(nodes => nodes.map((node, index) => {
    const text = (node.innerText || node.textContent || "").trim().replace(/\s+/g, " ");
    const cls = node.className || "";
    return {index, text, cls: String(cls)};
  }).filter(m => m.text));
  if (messages.length) return messages.slice(-50);
  const body = (await page.locator("body").innerText().catch(() => "")).trim();
  return body ? [{index: 0, text: body.slice(-8000), cls: "body-fallback"}] : [];
}

app.get("/health", (_, res) => res.json({ok: true}));

app.post("/login", requireAuth, async (req, res) => {
  const {accountId, email, password} = req.body || {};
  if (!accountId || !email || !password) return res.status(400).json({error: "accountId, email and password are required"});
  let context;
  try {
    ({context, page} = await openPage(accountId));
    const result = await login(page, String(email), String(password));
    res.status(result.ok ? 200 : 401).json(result);
  } catch (error) {
    res.status(500).json({error: error.message});
  } finally {
    if (context) await closeContext(context);
  }
});

app.post("/status", requireAuth, async (req, res) => {
  const {accountId} = req.body || {};
  if (!accountId) return res.status(400).json({error: "accountId is required"});
  let context;
  try {
    const opened = await openPage(accountId);
    context = opened.context;
    await opened.page.goto(BASE, {waitUntil: "domcontentloaded"});
    res.json({ok: true, loggedIn: await isLoggedIn(opened.page), url: opened.page.url()});
  } catch (error) {
    res.status(500).json({error: error.message});
  } finally {
    if (context) await closeContext(context);
  }
});

app.post("/send", requireAuth, async (req, res) => {
  const {accountId, listingUrl, message} = req.body || {};
  if (!accountId || !listingUrl || !message) return res.status(400).json({error: "accountId, listingUrl and message are required"});
  let context;
  try {
    const opened = await openPage(accountId);
    context = opened.context;
    if (!await isLoggedIn(opened.page)) return res.status(401).json({error: "CustoJusto session is not logged in"});
    res.json(await sendMessage(opened.page, String(listingUrl), String(message)));
  } catch (error) {
    res.status(500).json({error: error.message});
  } finally {
    if (context) await closeContext(context);
  }
});

app.post("/incoming", requireAuth, async (req, res) => {
  const {accountId} = req.body || {};
  if (!accountId) return res.status(400).json({error: "accountId is required"});
  let context;
  try {
    const opened = await openPage(accountId);
    context = opened.context;
    if (!await isLoggedIn(opened.page)) return res.status(401).json({error: "CustoJusto session is not logged in"});
    await opened.page.goto(new URL("/mensagens", BASE).toString(), {waitUntil: "domcontentloaded"});
    const conversations = await extractMessages(opened.page);
    const result = [];
    for (const conversation of conversations.slice(0, 20)) {
      if (!conversation.href) continue;
      const messages = await collectConversation(opened.page, conversation.href);
      result.push({conversation, messages});
    }
    res.json({ok: true, conversations: result});
  } catch (error) {
    res.status(500).json({error: error.message});
  } finally {
    if (context) await closeContext(context);
  }
});

app.listen(PORT, "0.0.0.0", () => {
  console.log(`CustoJusto browser worker listening on ${PORT}`);
});
