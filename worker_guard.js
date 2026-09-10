const express = require("express");

const PORT = Number(process.env.BROWSER_WORKER_PORT || 3001);
const UPSTREAM_PORT = Number(process.env.BROWSER_WORKER_UPSTREAM_PORT || 3002);
const MAX_BODY_BYTES = "96kb";
const app = express();
app.disable("x-powered-by");
app.use(express.json({ limit: MAX_BODY_BYTES, strict: true }));

function custoJustoUrl(value, { required = false } = {}) {
  const raw = String(value || "").trim();
  if (!raw) {
    if (required) throw new Error("A CustoJusto URL is required.");
    return;
  }
  let url;
  try { url = new URL(raw); } catch { throw new Error("Invalid CustoJusto URL."); }
  const host = url.hostname.toLowerCase().replace(/\.$/, "");
  const allowedHost = host === "custojusto.pt" || host.endsWith(".custojusto.pt");
  if (url.protocol !== "https:" || !allowedHost || url.username || url.password || (url.port && url.port !== "443")) {
    throw new Error("Only HTTPS URLs on custojusto.pt are allowed.");
  }
}

function validate(path, body) {
  switch (path) {
    case "/messages":
    case "/send":
      custoJustoUrl(body.conversationUrl, { required: true });
      break;
    case "/listing":
      custoJustoUrl(body.listingUrl, { required: true });
      break;
    case "/conversations":
    case "/manual/prepare":
      custoJustoUrl(body.baseUrl);
      break;
    default:
      break;
  }
}

app.use(async (req, res) => {
  try {
    if (!/^\/[A-Za-z0-9_./-]*$/.test(req.path)) return res.status(400).type("text/plain").send("Invalid worker path.");
    validate(req.path, req.body || {});
    const headers = {};
    if (req.headers["x-worker-secret"]) headers["x-worker-secret"] = String(req.headers["x-worker-secret"]);
    if (req.method !== "GET" && req.method !== "HEAD") headers["content-type"] = "application/json";
    const upstream = await fetch(`http://127.0.0.1:${UPSTREAM_PORT}${req.originalUrl}`, {
      method: req.method,
      headers,
      body: req.method === "GET" || req.method === "HEAD" ? undefined : JSON.stringify(req.body || {})
    });
    const payload = Buffer.from(await upstream.arrayBuffer());
    const contentType = upstream.headers.get("content-type");
    if (contentType) res.set("content-type", contentType);
    res.status(upstream.status).send(payload);
  } catch (error) {
    res.status(error instanceof SyntaxError ? 400 : 400).json({ error: String(error.message || error) });
  }
});

app.listen(PORT, "127.0.0.1", () => console.log(`Browser worker guard listening on ${PORT}, upstream ${UPSTREAM_PORT}`));
