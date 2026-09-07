const express = require("express");
const http = require("http");
const httpProxy = require("http-proxy");
const crypto = require("crypto");

const PORT = Number(process.env.PORT || 8080);
const PASSWORD = process.env.REMOTE_BROWSER_PASSWORD || "";
const WORKER_SECRET = process.env.BROWSER_WORKER_SHARED_SECRET || "";
const SESSION_TTL_MS = 8 * 60 * 60 * 1000;
const sessions = new Map();
const proxy = httpProxy.createProxyServer({ target: "http://127.0.0.1:6080", ws: true });
const app = express();
app.disable("x-powered-by");
app.use(express.urlencoded({ extended: false, limit: "8kb" }));

function cookies(header) {
  return Object.fromEntries(String(header || "").split(";").map(part => {
    const i = part.indexOf("=");
    return i < 0 ? ["", ""] : [part.slice(0, i).trim(), decodeURIComponent(part.slice(i + 1).trim())];
  }).filter(([key]) => key));
}
function session(req) {
  const token = cookies(req.headers.cookie).cj_browser_session;
  const expiresAt = sessions.get(token);
  if (!expiresAt || expiresAt < Date.now()) { if (token) sessions.delete(token); return false; }
  return true;
}
function basic(req) {
  const auth = String(req.headers.authorization || "");
  if (!PASSWORD || !auth.startsWith("Basic ")) return false;
  let value; try { value = Buffer.from(auth.slice(6), "base64").toString("utf8"); } catch (_) { return false; }
  const i = value.indexOf(":"); if (i < 0) return false;
  const user = Buffer.from(value.slice(0, i)), password = Buffer.from(value.slice(i + 1));
  const expectedUser = Buffer.from("custo"), expectedPassword = Buffer.from(PASSWORD);
  return user.length === expectedUser.length && password.length === expectedPassword.length && crypto.timingSafeEqual(user, expectedUser) && crypto.timingSafeEqual(password, expectedPassword);
}
function requireBasic(req, res, next) { if (basic(req)) return next(); res.set("WWW-Authenticate", 'Basic realm="CustoJusto browser"').status(401).type("text/plain").send("Authentication required."); }
function requireSession(req, res, next) { if (session(req)) return next(); res.status(401).type("text/plain").send("Open the CustoJusto link from Telegram first."); }
function createSession(res) { const token = crypto.randomBytes(32).toString("hex"); sessions.set(token, Date.now() + SESSION_TTL_MS); res.cookie("cj_browser_session", token, { httpOnly:true, secure:true, sameSite:"lax", path:"/", maxAge:SESSION_TTL_MS }); }
function accountId(value) { const id = String(value || "").trim(); if (!/^[A-Za-z0-9_-]{1,100}$/.test(id)) throw new Error("Invalid account id."); return id; }
async function worker(path, options = {}) { const response = await fetch(`http://127.0.0.1:3001${path}`, { ...options, headers: { "x-worker-secret": WORKER_SECRET, ...(options.headers || {}) } }); if (!response.ok) throw new Error((await response.text()).slice(0, 250) || "Browser worker request failed."); return response; }
function form(account, error = "") { const safe = String(account || "").replace(/[^A-Za-z0-9_-]/g, ""); const notice = error ? `<p class="error">${error}</p>` : ""; return `<!doctype html><html lang="ru"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>CustoJusto</title><style>body{margin:0;background:#111827;color:#fff;font:17px -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}.wrap{max-width:440px;margin:auto;padding:28px 20px 44px}h1{font-size:27px;margin:8px 0 12px}p{line-height:1.45;color:#cbd5e1}.card{background:#fff;color:#172033;border-radius:18px;padding:22px;margin-top:24px}label{display:block;font-weight:650;margin:16px 0 7px}input{box-sizing:border-box;width:100%;font:18px inherit;padding:15px;border:1px solid #b9c3d0;border-radius:10px;background:#fff;color:#111}button{width:100%;margin-top:22px;border:0;border-radius:10px;padding:16px;background:#f97316;color:#fff;font:700 18px inherit}small{display:block;margin-top:17px;color:#64748b;line-height:1.4}.error{color:#b91c1c;background:#fee2e2;padding:12px;border-radius:9px}</style><main class="wrap"><h1>Вход в CustoJusto</h1><p>Вставь email и пароль здесь. На следующем экране откроется браузер — только для CAPTCHA или подтверждения.</p><section class="card">${notice}<form method="post" action="/browser-api/manual/open"><input type="hidden" name="accountId" value="${safe}"><label>Email CustoJusto</label><input name="email" type="email" autocomplete="username" autocapitalize="none" spellcheck="false" required><label>Пароль CustoJusto</label><input name="password" type="password" autocomplete="current-password" required><button type="submit">Открыть браузер</button></form><small>Логин и пароль передаются только в временный браузерный профиль, не сохраняются Telegram-ботом.</small></section></main></html>`; }
setInterval(() => { const now = Date.now(); for (const [token, until] of sessions) if (until < now) sessions.delete(token); }, 3600000).unref();
app.get("/health", (_, res) => res.type("text/plain").send("ok"));
app.get("/browser-api/manual/open", requireBasic, (req, res) => { try { res.type("html").send(form(accountId(req.query.accountId))); } catch (error) { res.status(400).type("text/plain").send(error.message); } });
app.post("/browser-api/manual/open", requireBasic, async (req, res) => { let account = ""; try { account = accountId(req.body.accountId); const email = String(req.body.email || "").trim(), password = String(req.body.password || ""); if (!email || !password) return res.status(400).type("html").send(form(account, "Заполни email и пароль.")); await worker("/manual/prepare", { method:"POST", headers:{"content-type":"application/json"}, body:JSON.stringify({accountId:account,email,password}) }); createSession(res); res.redirect(302, "/vnc.html?autoconnect=true&resize=scale&show_dot=true"); } catch (error) { res.status(502).type("html").send(form(account, `Не удалось открыть браузер: ${error.message}`)); } });
app.all("/websockify*", requireSession, (req, res) => proxy.web(req, res));
app.use(requireSession, express.static("/usr/share/novnc", { fallthrough:false }));
const server = http.createServer(app);
server.on("upgrade", (req, socket, head) => { if (!req.url.startsWith("/websockify") || !session(req)) { socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n"); return socket.destroy(); } proxy.ws(req, socket, head); });
proxy.on("error", (_error, _req, res) => { if (res && !res.headersSent) res.writeHead(502); if (res) res.end("Browser connection unavailable."); });
server.listen(PORT, "0.0.0.0", () => console.log(`Browser gateway listening on ${PORT}`));
