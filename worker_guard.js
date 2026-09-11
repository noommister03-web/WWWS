const express = require("express");
const crypto = require("crypto");

const PORT = Number(process.env.BROWSER_WORKER_PORT || 3001);
const UPSTREAM_PORT = Number(process.env.BROWSER_WORKER_UPSTREAM_PORT || 3002);
const SECRET = process.env.BROWSER_WORKER_SHARED_SECRET || "";
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
function authorized(req) {
  if (!SECRET) return false;
  const provided=Buffer.from(String(req.headers["x-worker-secret"]||"")), expected=Buffer.from(SECRET);
  return provided.length===expected.length && crypto.timingSafeEqual(provided,expected);
}
const routes = new Set([
  "GET /health", "GET /manual/open", "POST /manual/prepare", "POST /login",
  "POST /status", "POST /conversations", "POST /messages", "POST /send", "POST /listing"
]);
function validate(req) {
  const key=`${req.method} ${req.path}`;
  if(!routes.has(key)) { const error=new Error("Worker endpoint is not allowed."); error.status=404; throw error; }
  if(req.path==="/manual/open") custoJustoUrl(req.query.baseUrl);
  else if(["/manual/prepare","/login","/status","/conversations"].includes(req.path)) custoJustoUrl(req.body?.baseUrl);
  else if(["/messages","/send"].includes(req.path)) custoJustoUrl(req.body?.conversationUrl,{required:true});
  else if(req.path==="/listing") custoJustoUrl(req.body?.listingUrl,{required:true});
  if(req.method==="POST"&&!authorized(req)) { const error=new Error(SECRET?"Unauthorized":"Worker secret is not configured."); error.status=SECRET?401:503; throw error; }
}
function decodeHtml(value){return String(value||"").replace(/&amp;/g,"&").replace(/&quot;/g,'"').replace(/&#39;/g,"'").replace(/&lt;/g,"<").replace(/&gt;/g,">").replace(/\s+/g," ").trim();}
function meta(html,names){for(const name of names){const escaped=name.replace(/[.*+?^${}()|[\]\\]/g,"\\$&");for(const pattern of [new RegExp(`<meta[^>]+(?:property|name)=["']${escaped}["'][^>]+content=["']([^"']*)["']`,`i`),new RegExp(`<meta[^>]+content=["']([^"']*)["'][^>]+(?:property|name)=["']${escaped}["']`,`i`)]){const match=html.match(pattern);if(match?.[1])return decodeHtml(match[1]);}}return"";}
function jsonLd(html){const values=[],expression=/<script[^>]+type=["']application\/ld\+json["'][^>]*>([\s\S]*?)<\/script>/gi;for(let match;(match=expression.exec(html));){try{const value=JSON.parse(match[1]);values.push(...(Array.isArray(value)?value:[value]));}catch{}}return values.flatMap(value=>value?.["@graph"]&&Array.isArray(value["@graph"])?value["@graph"]:[value]).find(value=>/Product|Offer/.test(String(value?.["@type"]||"")))||{};}
async function safeListingFetch(initial){let current=custoJustoUrl(initial,{required:true});for(let redirects=0;redirects<4;redirects++){const response=await fetch(current,{redirect:"manual",headers:{"user-agent":"Mozilla/5.0 (compatible; WWWS/1.0)",accept:"text/html,application/xhtml+xml"}});if([301,302,303,307,308].includes(response.status)){const location=response.headers.get("location");if(!location)throw new Error("Listing redirect has no location.");current=custoJustoUrl(new URL(location,current).toString(),{required:true});continue;}if(!response.ok)throw new Error(`Listing HTTP ${response.status}`);return{url:current.toString(),html:await response.text()};}throw new Error("Too many listing redirects.");}
async function listing(body){const loaded=await safeListingFetch(body.listingUrl),html=loaded.html,ld=jsonLd(html),offer=Array.isArray(ld.offers)?ld.offers[0]||{}:ld.offers||{};return{url:loaded.url,title:decodeHtml(ld.name)||meta(html,["og:title","twitter:title"])||decodeHtml((html.match(/<title[^>]*>([^<]*)<\/title>/i)||[])[1]),description:decodeHtml(ld.description)||meta(html,["og:description","description"]),price:String(offer.price||ld.price||meta(html,["product:price:amount","og:price:amount"])),currency:offer.priceCurrency||meta(html,["product:price:currency"]),sellerName:decodeHtml(ld.seller?.name||offer.seller?.name||""),location:decodeHtml(ld.availableAtOrFrom?.address?.addressLocality||ld.areaServed?.name||"")};}

app.use(async(req,res)=>{
  try{
    validate(req);
    if(req.method==="GET"&&req.path==="/health")return res.json({ok:true,guard:true});
    if(req.path==="/listing")return res.json(await listing(req.body||{}));
    // Safe diagnostic mode: verify an existing message through the read-only
    // /messages endpoint. Never forward verifyOnly requests to /send.
    if(req.method==="POST"&&req.path==="/send"&&req.body?.verifyOnly===true){
      const upstream=await fetch(`http://127.0.0.1:${UPSTREAM_PORT}/messages`,{method:"POST",headers:{"content-type":"application/json","x-worker-secret":String(req.headers["x-worker-secret"]||"")},body:JSON.stringify({accountId:req.body.accountId,conversationUrl:req.body.conversationUrl,fullHistory:false})});
      const payload=await upstream.json().catch(()=>null);
      if(!upstream.ok)return res.status(upstream.status).json(payload||{error:"Read-only message verification failed."});
      const expected=String(req.body.text||"").trim().replace(/\s+/g," ");
      const found=Array.isArray(payload)&&expected.length>0&&payload.some(message=>String(message?.text||"").trim().replace(/\s+/g," ").includes(expected));
      return res.json({ok:found,verified:true,proof:found?"conversation-before-send":"not-found",url:req.body.conversationUrl});
    }
    const headers={};if(req.headers["x-worker-secret"])headers["x-worker-secret"]=String(req.headers["x-worker-secret"]);if(req.method!=="GET"&&req.method!=="HEAD")headers["content-type"]="application/json";
    const upstream=await fetch(`http://127.0.0.1:${UPSTREAM_PORT}${req.originalUrl}`,{method:req.method,headers,redirect:"manual",body:req.method==="GET"||req.method==="HEAD"?undefined:JSON.stringify(req.body||{})});
    if([301,302,303,307,308].includes(upstream.status)){const location=upstream.headers.get("location");if(!location)throw new Error("Upstream redirect has no location.");const target=new URL(location,`http://127.0.0.1:${UPSTREAM_PORT}`);if(target.hostname!=="127.0.0.1"||Number(target.port||80)!==UPSTREAM_PORT)throw new Error("Unsafe upstream redirect blocked.");res.redirect(upstream.status,target.pathname+target.search);return;}
    const payload=Buffer.from(await upstream.arrayBuffer()),contentType=upstream.headers.get("content-type");if(contentType)res.set("content-type",contentType);res.status(upstream.status).send(payload);
  }catch(error){res.status(Number(error.status)||400).json({error:String(error.message||error)});}
});
app.listen(PORT,"127.0.0.1",()=>console.log(`Browser worker guard listening on ${PORT}, upstream ${UPSTREAM_PORT}`));
