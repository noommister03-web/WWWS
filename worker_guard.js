const express = require("express");
const crypto = require("crypto");

const PORT = Number(process.env.BROWSER_WORKER_PORT || 3001);
const UPSTREAM_PORT = Number(process.env.BROWSER_WORKER_UPSTREAM_PORT || 3002);
const app = express();
app.disable("x-powered-by");
app.use(express.json({ limit: "96kb", strict: true }));

function custoJustoUrl(value, { required = false } = {}) {
  const raw = String(value || "").trim();
  if (!raw) { if (required) throw new Error("A CustoJusto URL is required."); return null; }
  let url;
  try { url = new URL(raw); } catch { throw new Error("Invalid CustoJusto URL."); }
  const host = url.hostname.toLowerCase().replace(/\.$/, "");
  if (url.protocol !== "https:" || !(host === "custojusto.pt" || host.endsWith(".custojusto.pt")) || url.username || url.password || (url.port && url.port !== "443")) {
    throw new Error("Only HTTPS URLs on custojusto.pt are allowed.");
  }
  return url;
}

function validate(path, body) {
  if (path === "/messages" || path === "/send") custoJustoUrl(body.conversationUrl, { required: true });
  else if (path === "/listing") custoJustoUrl(body.listingUrl, { required: true });
  else if (path === "/conversations" || path === "/manual/prepare") custoJustoUrl(body.baseUrl);
}

function decodeHtml(value) {
  return String(value || "").replace(/&amp;/g,"&").replace(/&quot;/g,'"').replace(/&#39;/g,"'").replace(/&lt;/g,"<").replace(/&gt;/g,">").replace(/\s+/g," ").trim();
}
function meta(html, names) {
  for (const name of names) {
    const escaped=name.replace(/[.*+?^${}()|[\]\\]/g,"\\$&");
    const patterns=[new RegExp(`<meta[^>]+(?:property|name)=["']${escaped}["'][^>]+content=["']([^"']*)["']`,`i`),new RegExp(`<meta[^>]+content=["']([^"']*)["'][^>]+(?:property|name)=["']${escaped}["']`,`i`)];
    for (const pattern of patterns) { const match=html.match(pattern); if (match?.[1]) return decodeHtml(match[1]); }
  }
  return "";
}
function jsonLd(html) {
  const values=[]; const expression=/<script[^>]+type=["']application\/ld\+json["'][^>]*>([\s\S]*?)<\/script>/gi;
  for (let match; (match=expression.exec(html)); ) { try { const value=JSON.parse(match[1]); values.push(...(Array.isArray(value)?value:[value])); } catch {} }
  return values.flatMap(value => value?.["@graph"] && Array.isArray(value["@graph"])?value["@graph"]:[value]).find(value => /Product|Offer/.test(String(value?.["@type"] || ""))) || {};
}
async function safeListingFetch(initial) {
  let current=custoJustoUrl(initial,{required:true});
  for (let redirects=0; redirects<4; redirects++) {
    const response=await fetch(current,{redirect:"manual",headers:{"user-agent":"Mozilla/5.0 (compatible; WWWS/1.0)",accept:"text/html,application/xhtml+xml"}});
    if ([301,302,303,307,308].includes(response.status)) { const location=response.headers.get("location"); if(!location) throw new Error("Listing redirect has no location."); current=custoJustoUrl(new URL(location,current).toString(),{required:true}); continue; }
    if(!response.ok) throw new Error(`Listing HTTP ${response.status}`);
    return {url:current.toString(),html:await response.text()};
  }
  throw new Error("Too many listing redirects.");
}
function normalizeMessages(rows, conversationUrl) {
  if(!Array.isArray(rows)) return rows;
  const occurrences=new Map();
  return rows.map(row => {
    const canonical=`${row.incoming?"in":"out"}|${row.sender||""}|${row.timestamp||""}|${row.text||""}`;
    const occurrence=(occurrences.get(canonical)||0)+1; occurrences.set(canonical,occurrence);
    const synthetic=!row.id || /^[a-f0-9]{32}$/i.test(String(row.id));
    const id=synthetic?crypto.createHash("sha256").update(`${conversationUrl}|${canonical}|from-start-${occurrence}`).digest("hex").slice(0,32):row.id;
    return {...row,id};
  });
}
async function transform(path, body, status, contentType, payload) {
  if(status<200 || status>=300 || !String(contentType||"").includes("json")) return payload;
  let data; try { data=JSON.parse(payload.toString("utf8")); } catch { return payload; }
  if(path==="/messages") data=normalizeMessages(data,String(body.conversationUrl||""));
  if(path==="/listing") {
    try {
      const loaded=await safeListingFetch(body.listingUrl); const html=loaded.html, ld=jsonLd(html), offer=ld.offers||{};
      data.url=loaded.url; data.title=data.title||decodeHtml(ld.name)||meta(html,["og:title","twitter:title"]); data.description=data.description||decodeHtml(ld.description)||meta(html,["og:description","description"]);
      data.price=data.price||String(offer.price||ld.price||meta(html,["product:price:amount","og:price:amount"])); data.currency=data.currency||offer.priceCurrency||meta(html,["product:price:currency"]);
      data.sellerName=data.sellerName||decodeHtml(ld.seller?.name||offer.seller?.name||""); data.location=data.location||decodeHtml(ld.availableAtOrFrom?.address?.addressLocality||ld.areaServed?.name||"");
    } catch (error) { data.enrichmentWarning=String(error.message||error); }
  }
  return Buffer.from(JSON.stringify(data));
}

app.use(async (req,res) => {
  try {
    if(!/^\/[A-Za-z0-9_./-]*$/.test(req.path)) return res.status(400).type("text/plain").send("Invalid worker path.");
    validate(req.path,req.body||{});
    const headers={}; if(req.headers["x-worker-secret"]) headers["x-worker-secret"]=String(req.headers["x-worker-secret"]); if(req.method!=="GET"&&req.method!=="HEAD") headers["content-type"]="application/json";
    const upstream=await fetch(`http://127.0.0.1:${UPSTREAM_PORT}${req.originalUrl}`,{method:req.method,headers,body:req.method==="GET"||req.method==="HEAD"?undefined:JSON.stringify(req.body||{})});
    const contentType=upstream.headers.get("content-type")||""; let payload=Buffer.from(await upstream.arrayBuffer()); payload=await transform(req.path,req.body||{},upstream.status,contentType,payload);
    if(contentType) res.set("content-type",contentType); res.status(upstream.status).send(payload);
  } catch(error) { res.status(error instanceof SyntaxError?400:400).json({error:String(error.message||error)}); }
});
app.listen(PORT,"127.0.0.1",()=>console.log(`Browser worker guard listening on ${PORT}, upstream ${UPSTREAM_PORT}`));
