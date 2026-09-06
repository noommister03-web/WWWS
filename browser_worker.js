const express = require("express");
const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const app = express();
app.use(express.json({limit: "1mb"}));

const PORT = Number(process.env.PORT || process.env.BROWSER_WORKER_PORT || 3001);
const WORKER_SHARED_SECRET = process.env.BROWSER_WORKER_SHARED_SECRET || "";
const BASE = process.env.CJ_BASE_URL || "https://www.custojusto.pt";
const PROFILE_ROOT = process.env.CJ_PROFILE_ROOT || path.join(process.cwd(), "data", "custojusto", "profiles");

fs.mkdirSync(PROFILE_ROOT, {recursive: true});

const contexts = new Map();

function profileDir(accountId) {
  const safe = String(accountId).replace(/[^0-9A-Za-z_-]/g, "_");
  const dir = path.join(PROFILE_ROOT, safe);
  fs.mkdirSync(dir, {recursive: true});
  return dir;
}

async function getContext(accountId) {
  if (contexts.has(accountId)) return contexts.get(accountId);

  const context = await chromium.launchPersistentContext(profileDir(accountId), {
    headless: true,
    viewport: {width: 1440, height: 1000},
    locale: "pt-PT",
    timezoneId: "Europe/Lisbon",
    serviceWorkers: "allow",
    args: ["--disable-dev-shm-usage"]
  });

  contexts.set(accountId, context);
  return context;
}

async function getPage(context) {
  const pages = context.pages();
  return pages.length ? pages[0] : await context.newPage();
}

function pageText(page) {
  return page.locator("body").innerText({timeout: 5000}).catch(() => "");
}

async function detectState(page) {
  const text = (await pageText(page)).toLowerCase();
  const url = page.url().toLowerCase();

  const captcha =
    text.includes("captcha") ||
    text.includes("recaptcha") ||
    url.includes("captcha");

  const twoFactor =
    text.includes("cÃ³digo de verificaÃ§Ã£o") ||
    text.includes("codigo de verificacao") ||
    text.includes("two-factor") ||
    text.includes("2fa") ||
    text.includes("verificaÃ§Ã£o") && text.includes("sms");

  if (captcha) return {state:"captcha_required", loggedIn:false};
  if (twoFactor) return {state:"two_factor_required", loggedIn:false};

  const loginWords = ["iniciar sessÃ£o", "iniciar sessao", "entrar", "login"];
  const hasLogin = loginWords.some(x => text.includes(x));
  const hasChat = text.includes("chat") || text.includes("mensagens");
  const accountHints =
    text.includes("terminar sessÃ£o") ||
    text.includes("terminar sessao") ||
    text.includes("logout") ||
    text.includes("minha conta") ||
    text.includes("meus anÃºncios") ||
    text.includes("meus anuncios");

  if (hasChat && (accountHints || !hasLogin)) {
    return {state:"logged_in", loggedIn:true};
  }

  return {state:"login_required", loggedIn:false};
}

async function clickLoginIfNeeded(page) {
  const selectors = [
    'a:has-text("Entrar")',
    'button:has-text("Entrar")',
    'a:has-text("Iniciar sessÃ£o")',
    'button:has-text("Iniciar sessÃ£o")',
    'a:has-text("Login")',
    'button:has-text("Login")'
  ];

  for (const selector of selectors) {
    const el = page.locator(selector).first();
    if (await el.count()) {
      try {
        await el.click({timeout: 4000});
        await page.waitForLoadState("domcontentloaded", {timeout: 10000}).catch(()=>{});
        return true;
      } catch (_) {}
    }
  }
  return false;
}

async function fillFirst(page, candidates, value) {
  for (const selector of candidates) {
    const el = page.locator(selector).first();
    if (await el.count()) {
      try {
        await el.fill(value, {timeout: 4000});
        return true;
      } catch (_) {}
    }
  }
  return false;
}

async function doLogin(accountId, email, password, baseUrl) {
  const context = await getContext(accountId);
  const page = await getPage(context);

  await page.goto(baseUrl || BASE, {waitUntil:"domcontentloaded", timeout:30000});
  const before = await detectState(page);
  if (before.loggedIn) return before;

  await clickLoginIfNeeded(page);

  const emailOk = await fillFirst(page, [
    'input[type="email"]',
    'input[name="email"]',
    'input[autocomplete="username"]',
    'input[placeholder*="email" i]'
  ], email);

  const passOk = await fillFirst(page, [
    'input[type="password"]',
    'input[name="password"]',
    'input[autocomplete="current-password"]'
  ], password);

  if (!emailOk || !passOk) {
    return {state:"login_form_not_found", loggedIn:false};
  }

  const buttons = [
    'button[type="submit"]',
    'input[type="submit"]',
    'button:has-text("Entrar")',
    'button:has-text("Login")',
    'button:has-text("Iniciar sessÃ£o")'
  ];

  let clicked = false;
  for (const selector of buttons) {
    const el = page.locator(selector).first();
    if (await el.count()) {
      try {
        await el.click({timeout:5000});
        clicked = true;
        break;
      } catch (_) {}
    }
  }

  if (!clicked) return {state:"login_button_not_found", loggedIn:false};

  await page.waitForLoadState("domcontentloaded", {timeout:15000}).catch(()=>{});
  await page.waitForTimeout(1500);

  return await detectState(page);
}

async function openChat(page) {
  const selectors = [
    'a:has-text("Chat")',
    'button:has-text("Chat")',
    'a:has-text("Mensagens")',
    'button:has-text("Mensagens")',
    '[href*="chat"]',
    '[href*="mensag"]'
  ];

  for (const selector of selectors) {
    const el = page.locator(selector).first();
    if (await el.count()) {
      try {
        await el.click({timeout:5000});
        await page.waitForLoadState("domcontentloaded", {timeout:10000}).catch(()=>{});
        await page.waitForTimeout(700);
        return true;
      } catch (_) {}
    }
  }

  if (page.url().toLowerCase().includes("chat")) return true;
  return false;
}

function absoluteUrl(href, base) {
  try { return new URL(href, base).toString(); } catch (_) { return href || ""; }
}

async function conversations(accountId, baseUrl) {
  const context = await getContext(accountId);
  const page = await getPage(context);
  await page.goto(baseUrl || BASE, {waitUntil:"domcontentloaded", timeout:30000});
  const state = await detectState(page);
  if (!state.loggedIn) return {ok:false, error:state.state};
  if (!(await openChat(page))) return {ok:false, error:"chat_not_found"};

  const data = await page.locator("a[href]").evaluateAll((els) => {
    const out = [], seen = new Set();
    for (const el of els) {
      const href = el.href || el.getAttribute("href") || "";
      const text = (el.innerText || el.textContent || "").trim().replace(/\s+/g, " ");
      if (!href || !text || href === location.href) continue;
      const lower = href.toLowerCase();
      if (!/(chat|mensag|conversation|conversa)/.test(lower)) continue;
      if (seen.has(href)) continue;
      seen.add(href);
      out.push({id:href, url:href, title:text.slice(0, 250), unread:/\b\d+\b|nova|novo|unread/i.test(text)});
    }
    return out.slice(0, 100);
  });
  return {ok:true, conversations:data};
}
async function messages(accountId, conversationUrl) {
  const context = await getContext(accountId);
  const page = await getPage(context);
  await page.goto(conversationUrl, {waitUntil:"domcontentloaded", timeout:30000});
  const state = await detectState(page);
  if (!state.loggedIn) return {ok:false, error:state.state};

  const items = await page.locator("[data-message-id], [data-testid*='message'], .message, [role='listitem']").evaluateAll((els) =>
    els.map((el, index) => {
      const text = (el.innerText || el.textContent || "").trim().replace(/\s+/g, " ");
      const cls = String(el.className || "").toLowerCase();
      const id = el.getAttribute("data-message-id") || el.getAttribute("id") || `${index}:${text}`;
      return {
        id, text: text.slice(0, 4000),
        sender: el.getAttribute("data-sender") || "",
        incoming: !/(outgoing|sent|mine|own-message)/.test(cls),
        timestamp: el.getAttribute("data-timestamp") || ""
      };
    }).filter(x => x.text)
  );
  return {ok:true, messages:items.slice(-100)};
}
async function send(accountId, conversationUrl, text) {
  const context = await getContext(accountId);
  const page = await getPage(context);
  await page.goto(conversationUrl, {waitUntil:"domcontentloaded", timeout:30000});
  const state = await detectState(page);
  if (!state.loggedIn) return {ok:false, error:state.state};

  const inputSelectors = [
    'textarea',
    'textarea[placeholder*="mensagem" i]',
    'textarea[placeholder*="message" i]',
    'input[type="text"]'
  ];

  // A listing page needs one extra click before its message composer exists.
  if (!(await page.locator(inputSelectors.join(",")).count())) {
    for (const selector of [
      'a:has-text("Contactar")', 'button:has-text("Contactar")',
      'a:has-text("Enviar mensagem")', 'button:has-text("Enviar mensagem")',
      'a:has-text("Mensagem")', 'button:has-text("Mensagem")'
    ]) {
      const el = page.locator(selector).first();
      if (await el.count()) {
        try {
          await el.click({timeout:5000});
          await page.waitForTimeout(700);
          break;
        } catch (_) {}
      }
    }
  }

  let filled = false;
  for (const selector of inputSelectors) {
    const el = page.locator(selector).last();
    if (await el.count()) {
      try {
        await el.fill(text, {timeout:5000});
        filled = true;
        break;
      } catch (_) {}
    }
  }
  if (!filled) return {ok:false, error:"message_input_not_found"};

  for (const selector of [
    'button[type="submit"]',
    'button:has-text("Enviar")',
    'button:has-text("Send")'
  ]) {
    const el = page.locator(selector).last();
    if (await el.count()) {
      try {
        await el.click({timeout:5000});
        await page.waitForTimeout(500);
        return {ok:true};
      } catch (_) {}
    }
  }

  return {ok:false, error:"send_button_not_found"};
}

app.get("/health", (_, res) => res.json({ok:true}));

app.use((req, res, next) => {
  if (!WORKER_SHARED_SECRET) {
    return res.status(503).json({ok:false, error:"worker_secret_not_configured"});
  }
  if (req.get("x-worker-secret") !== WORKER_SHARED_SECRET) {
    return res.status(401).json({ok:false, error:"unauthorized"});
  }
  return next();
});

app.post("/login", async (req,res) => {
  try {
    const {accountId,email,password,baseUrl} = req.body || {};
    if (!accountId || !email || !password) return res.status(400).json({ok:false,error:"missing_fields"});
    const result = await doLogin(accountId,email,password,baseUrl);
    res.json({ok:true,...result});
  } catch (e) {
    res.status(500).json({ok:false,error:String(e)});
  }
});

app.post("/check", async (req,res) => {
  try {
    const {accountId,baseUrl} = req.body || {};
    const context = await getContext(accountId);
    const page = await getPage(context);
    await page.goto(baseUrl || BASE, {waitUntil:"domcontentloaded", timeout:30000});
    const result = await detectState(page);
    res.json({ok:true,...result});
  } catch (e) {
    res.status(500).json({ok:false,error:String(e)});
  }
});

app.post("/conversations", async (req,res) => {
  try {
    const result = await conversations(req.body.accountId, req.body.baseUrl);
    res.json(result);
  } catch (e) {
    res.status(500).json({ok:false,error:String(e)});
  }
});

app.post("/messages", async (req,res) => {
  try {
    const result = await messages(req.body.accountId, req.body.conversationUrl);
    res.json(result);
  } catch (e) {
    res.status(500).json({ok:false,error:String(e)});
  }
});

app.post("/send", async (req,res) => {
  try {
    const {accountId,conversationUrl,text} = req.body || {};
    if (!accountId || !conversationUrl || !text) return res.status(400).json({ok:false,error:"missing_fields"});
    const result = await send(accountId,conversationUrl,text);
    res.json(result);
  } catch (e) {
    res.status(500).json({ok:false,error:String(e)});
  }
});

app.listen(PORT, "0.0.0.0", () => {
  console.log(`CustoJusto browser worker listening on ${PORT}`);
});
