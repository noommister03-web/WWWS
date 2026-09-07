const express = require("express");
const { chromium } = require("playwright");
const crypto = require("crypto");
const fsp = require("fs/promises");
const path = require("path");
const app = express();
app.use(express.json({ limit: "1mb" }));

const PORT = Number(process.env.BROWSER_WORKER_PORT || 3001);
const SECRET = process.env.BROWSER_WORKER_SHARED_SECRET || "";
const DEFAULT_BASE = process.env.CJ_BASE_URL || "https://www.custojusto.pt";
const PROFILE_ROOT = process.env.CJ_PROFILE_ROOT || "/app/data/custojusto/profiles";
const TIMEOUT = Number(process.env.CJ_ACTION_TIMEOUT_MS || 60000);

function safeId(value) { const id = String(value || "").trim(); if (!/^[A-Za-z0-9_-]{1,100}$/.test(id)) throw new Error("Invalid account id"); return id; }
function getBase(value) { const u = new URL(String(value || DEFAULT_BASE)); if (!/^https?:$/.test(u.protocol)) throw new Error("Invalid base URL"); return u.origin; }
function absolute(value, base) { try { return new URL(value, base).toString(); } catch { return ""; } }
function authorized(req, res, next) {
  if (!SECRET) return res.status(503).json({ error: "BROWSER_WORKER_SHARED_SECRET is not configured" });
  const actual = Buffer.from(req.get("x-worker-secret") || ""), expected = Buffer.from(SECRET);
  if (actual.length !== expected.length || !crypto.timingSafeEqual(actual, expected)) return res.status(401).json({ error: "Unauthorized" });
  next();
}
async function open(accountId) {
  if (process.env.CJ_MANUAL_BROWSER_MODE === "true") throw new Error("Manual browser mode is active. Complete login, then set CJ_MANUAL_BROWSER_MODE=false and redeploy before using the bridge.");
  const profile = path.join(PROFILE_ROOT, safeId(accountId));
  await fsp.mkdir(profile, { recursive: true });
  const context = await chromium.launchPersistentContext(profile, { headless: true, viewport: { width: 1365, height: 900 }, timeout: Number(process.env.CJ_BROWSER_LAUNCH_TIMEOUT_MS || 120000), args: ["--no-sandbox", "--disable-dev-shm-usage"] });
  const page = context.pages()[0] || await context.newPage(); page.setDefaultTimeout(TIMEOUT); return { context, page };
}
async function close(context) { if (context) await context.close().catch(() => {}); }
async function dismissCookies(page) {
  for (const selector of ["#CybotCookiebotDialogBodyButtonDecline", "#CybotCookiebotDialogBodyLevelButtonLevelOptinAllowAll", "button:has-text('Aceitar e fechar')"]) {
    const button = page.locator(selector).first(); if (await button.isVisible().catch(() => false)) { await button.click().catch(() => {}); return; }
  }
}
async function loggedIn(page) {
  if (/\/login|\/entrar|signin/i.test(page.url())) return false;
  if (await page.locator('a[href*="login"], a[href*="entrar"], button:has-text("Entrar")').first().isVisible().catch(() => false)) return false;
  return (await page.locator('a[href*="conta"], a[href*="account"], a[href*="mensagens"], a[href*="messages"]').count()) > 0;
}
async function withPage(req, res, handler) {
  let context;
  try { const opened = await open(req.body?.accountId); context = opened.context; await handler(opened.page, getBase(req.body?.baseUrl)); }
  catch (error) { if (!res.headersSent) res.status(500).json({ error: error.message }); }
  finally { await close(context); }
}
async function conversationList(page, base) {
  await page.goto(new URL("/mensagens", base).toString(), { waitUntil: "domcontentloaded", timeout: TIMEOUT }); await dismissCookies(page);
  const rows = await page.locator('a[href*="mensagens"], a[href*="messages"], a[href*="conversa"], a[href*="conversation"]').evaluateAll(nodes => nodes.map((n, index) => ({ id: n.getAttribute("data-conversation-id") || n.getAttribute("data-id") || `conversation-${index}`, url: n.href || n.getAttribute("href") || "", title: (n.innerText || n.textContent || "").trim().replace(/\s+/g, " ") })).filter(x => x.url));
  const seen = new Set(); return rows.map(x => ({ ...x, url: absolute(x.url, base), listingUrl: "", listingTitle: x.title, buyerName: "", lastMessage: "", lastMessageId: "", lastMessageAt: "", unread: false })).filter(x => x.url && !seen.has(x.url) && seen.add(x.url)).slice(0, 100);
}
async function messageList(page, url) {
  await page.goto(url, { waitUntil: "domcontentloaded", timeout: TIMEOUT });
  const rows = await page.locator('article, [data-message-id], .message, [class*="message"]').evaluateAll(nodes => nodes.map((n, index) => ({ id: n.getAttribute("data-message-id") || n.getAttribute("data-id") || `message-${index}`, sender: n.getAttribute("data-sender") || "", text: (n.innerText || n.textContent || "").trim().replace(/\s+/g, " "), timestamp: n.querySelector("time")?.getAttribute("datetime") || "", incoming: !/outgoing|sent|self/i.test(`${n.className} ${n.getAttribute("data-direction") || ""}`) })).filter(x => x.text));
  return rows.slice(-100).map(x => ({ ...x, conversationId: url }));
}
async function send(page, url, text) {
  await page.goto(url, { waitUntil: "domcontentloaded", timeout: TIMEOUT });
  for (const selector of ['a:has-text("Contactar")', 'button:has-text("Contactar")', 'a:has-text("Mensagem")', 'button:has-text("Mensagem")', 'a[href*="mensagens"]']) { const item = page.locator(selector).first(); if (await item.isVisible().catch(() => false)) { await item.click(); await page.waitForTimeout(600); break; } }
  const field = page.locator('textarea, [contenteditable="true"], input[name*="message" i], textarea[name*="message" i]').first(); await field.waitFor({ state: "visible" }); await field.fill(text); await page.locator('button[type="submit"], button:has-text("Enviar"), button:has-text("Send")').first().click(); return { ok: true, url: page.url() };
}

app.get("/health", (_, res) => res.json({ ok: true }));
app.post("/status", authorized, (req, res) => withPage(req, res, async (page, base) => { await page.goto(base, { waitUntil: "domcontentloaded", timeout: TIMEOUT }); res.json({ ok: true, loggedIn: await loggedIn(page), url: page.url() }); }));
app.post("/conversations", authorized, (req, res) => withPage(req, res, async (page, base) => { if (!await loggedIn(page)) return res.status(401).json({ error: "CustoJusto session is not logged in" }); res.json(await conversationList(page, base)); }));
app.post("/messages", authorized, (req, res) => withPage(req, res, async (page) => { if (!await loggedIn(page)) return res.status(401).json({ error: "CustoJusto session is not logged in" }); const url = String(req.body?.conversationUrl || ""); if (!url) return res.status(400).json({ error: "conversationUrl is required" }); res.json(await messageList(page, url)); }));
app.post("/send", authorized, (req, res) => withPage(req, res, async (page) => { if (!await loggedIn(page)) return res.status(401).json({ error: "CustoJusto session is not logged in" }); const url = String(req.body?.conversationUrl || req.body?.listingUrl || ""), text = String(req.body?.text || req.body?.message || ""); if (!url || !text) return res.status(400).json({ error: "conversationUrl and text are required" }); res.json(await send(page, url, text)); }));
app.post("/listing", authorized, async (req, res) => { const url = String(req.body?.listingUrl || ""); if (!url) return res.status(400).json({ error: "listingUrl is required" }); try { const response = await fetch(url); const html = await response.text(); res.json({ url, title: ((html.match(/<title[^>]*>([^<]*)<\/title>/i) || [, ""])[1]).trim(), price: "", sellerName: "", location: "" }); } catch (error) { res.status(500).json({ error: error.message }); } });
app.listen(PORT, "127.0.0.1", () => console.log(`CustoJusto browser worker listening on ${PORT}`));
