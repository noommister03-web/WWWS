const express = require("express");
const http = require("http");
const httpProxy = require("http-proxy");
const crypto = require("crypto");

const PORT = Number(process.env.PORT || 8080);
const PASSWORD = process.env.REMOTE_BROWSER_PASSWORD || "";
const SESSION_TTL_MS = 8 * 60 * 60 * 1000;
const sessions = new Map();
const proxy = httpProxy.createProxyServer({ target: "http://127.0.0.1:6080", ws: true });
const app = express();
app.disable("x-powered-by");

function parseCookies(header) {
  return Object.fromEntries(String(header || "").split(";").map(part => {
    const index = part.indexOf("=");
    return index < 0 ? ["", ""] : [part.slice(0, index).trim(), decodeURIComponent(part.slice(index + 1).trim())];
  }).filter(([key]) => key));
}

function validSession(req) {
  const token = parseCookies(req.headers.cookie).cj_browser_session;
  const expiresAt = sessions.get(token);
  if (!expiresAt || expiresAt < Date.now()) {
    if (token) sessions.delete(token);
    return false;
  }
  return true;
}

function requireSession(req, res, next) {
  if (validSession(req)) return next();
  res.status(401).type("text/plain").send("Open the CustoJusto login link from Telegram first.");
}

function validBasicAuth(req) {
  const header = String(req.headers.authorization || "");
  if (!header.startsWith("Basic ") || !PASSWORD) return false;
  let decoded;
  try { decoded = Buffer.from(header.slice(6), "base64").toString("utf8"); } catch (_) { return false; }
  const separator = decoded.indexOf(":");
  if (separator < 0) return false;
  const username = Buffer.from(decoded.slice(0, separator));
  const password = Buffer.from(decoded.slice(separator + 1));
  const expectedUser = Buffer.from("custo");
  const expectedPassword = Buffer.from(PASSWORD);
  return username.length === expectedUser.length && password.length === expectedPassword.length && crypto.timingSafeEqual(username, expectedUser) && crypto.timingSafeEqual(password, expectedPassword);
}

function challenge(res) {
  res.set("WWW-Authenticate", 'Basic realm="CustoJusto browser"');
  res.status(401).type("text/plain").send("Authentication required.");
}

function createSession(res) {
  const token = crypto.randomBytes(32).toString("hex");
  sessions.set(token, Date.now() + SESSION_TTL_MS);
  res.cookie("cj_browser_session", token, { httpOnly: true, secure: true, sameSite: "lax", path: "/", maxAge: SESSION_TTL_MS });
}

setInterval(() => {
  const now = Date.now();
  for (const [token, expiresAt] of sessions) if (expiresAt < now) sessions.delete(token);
}, 60 * 60 * 1000).unref();

app.get("/health", (_, res) => res.type("text/plain").send("ok"));

app.get("/browser-api/manual/open", async (req, res) => {
  if (!validBasicAuth(req)) return challenge(res);
  const query = new URLSearchParams();
  for (const key of ["accountId", "baseUrl", "mobile"]) {
    if (typeof req.query[key] === "string") query.set(key, req.query[key]);
  }
  try {
    const response = await fetch(`http://127.0.0.1:3001/manual/open?${query.toString()}`, { redirect: "manual" });
    if (response.status < 300 || response.status > 399) return res.status(response.status).type("text/plain").send(await response.text());
    const location = response.headers.get("location");
    if (!location || !location.startsWith("/")) return res.status(502).type("text/plain").send("Browser worker returned an invalid redirect.");
    createSession(res);
    return res.redirect(302, location);
  } catch (error) {
    return res.status(502).type("text/plain").send(`Unable to open browser: ${error.message}`);
  }
});

app.all("/websockify*", requireSession, (req, res) => proxy.web(req, res));
app.use(requireSession, express.static("/usr/share/novnc", { fallthrough: false }));
app.use((error, _req, res, _next) => {
  if (error && error.status === 404) return res.status(404).type("text/plain").send("Not found.");
  return res.status(500).type("text/plain").send("Browser gateway error.");
});

const server = http.createServer(app);
server.on("upgrade", (req, socket, head) => {
  if (!req.url.startsWith("/websockify") || !validSession(req)) {
    socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
    return socket.destroy();
  }
  proxy.ws(req, socket, head);
});
proxy.on("error", (_error, _req, res) => {
  if (res && !res.headersSent) res.writeHead(502);
  if (res) res.end("Browser connection is unavailable.");
});
server.listen(PORT, "0.0.0.0", () => console.log(`CustoJusto browser gateway listening on ${PORT}`));
