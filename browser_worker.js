const express = require("express");
const { chromium } = require("playwright");
const crypto = require("crypto");
const fs = require("fs/promises");
const path = require("path");
const app = express();
app.use(express.json({limit:"1mb"}));
const PORT=Number(process.env.BROWSER_WORKER_PORT||3001), SECRET=process.env.BROWSER_WORKER_SHARED_SECRET||"";
const DEFAULT_BASE=process.env.CJ_BASE_URL||"https://www.custojusto.pt", ROOT=process.env.CJ_PROFILE_ROOT||"/app/data/custojusto/profiles";
const TIMEOUT=Number(process.env.CJ_ACTION_TIMEOUT_MS||60000), contexts=new Map();
function id(v){v=String(v||"").trim();if(!/^[A-Za-z0-9_-]{1,100}$/.test(v))throw Error("Invalid account id");return v}
function base(v){const u=new URL(String(v||DEFAULT_BASE));if(!/^https?:$/.test(u.protocol))throw Error("Invalid base URL");return u.origin}
function auth(req,res,next){if(!SECRET)return res.status(503).json({error:"BROWSER_WORKER_SHARED_SECRET is not configured"});const a=Buffer.from(req.get("x-worker-secret")||""),b=Buffer.from(SECRET);if(a.length!==b.length||!crypto.timingSafeEqual(a,b))return res.status(401).json({error:"Unauthorized"});next()}
async function session(account){const key=id(account);if(contexts.has(key))return contexts.get(key);const profile=path.join(ROOT,key);await fs.mkdir(profile,{recursive:true});const context=await chromium.launchPersistentContext(profile,{headless:false,viewport:{width:1365,height:900},timeout:Number(process.env.CJ_BROWSER_LAUNCH_TIMEOUT_MS||120000),args:["--no-sandbox","--disable-dev-shm-usage","--start-maximized"]});const page=context.pages()[0]||await context.newPage();page.setDefaultTimeout(TIMEOUT);const s={context,page};contexts.set(key,s);context.on("close",()=>contexts.delete(key));return s}
async function cookies(page){for(const q of ["#CybotCookiebotDialogBodyButtonDecline","#CybotCookiebotDialogBodyLevelButtonLevelOptinAllowAll","button:has-text('Aceitar e fechar')"]){const b=page.locator(q).first();if(await b.isVisible().catch(()=>false)){await b.click().catch(()=>{});return}}}
async function logged(page){if(/\/login|\/entrar|signin/i.test(page.url()))return false;if(await page.locator('a[href*="login"],a[href*="entrar"],button:has-text("Entrar")').first().isVisible().catch(()=>false))return false;return(await page.locator('a[href*="conta"],a[href*="account"],a[href*="mensagens"],a[href*="messages"]').count())>0}
async function use(req,res,fn){try{const s=await session(req.body?.accountId);await fn(s.page,base(req.body?.baseUrl))}catch(e){if(!res.headersSent)res.status(500).json({error:e.message})}}
async function conversations(page,b){
  await page.goto(new URL("/mensagens",b).toString(),{waitUntil:"domcontentloaded",timeout:TIMEOUT});
  await cookies(page);
  await page.waitForTimeout(1000);
  const rows=await page.locator('a[href]').evaluateAll((links,origin)=>links.map((n,i)=>{
    const href=n.href||n.getAttribute("href")||"";
    let pathname="";try{pathname=new URL(href,origin).pathname.toLowerCase()}catch{}
    const text=(n.innerText||n.textContent||"").trim().replace(/\s+/g," ");
    return{id:n.getAttribute("data-conversation-id")||n.getAttribute("data-id")||`conversation-${i}`,url:href,title:text,pathname};
  }).filter(x=>x.url&&x.title&&x.pathname!=="/mensagens"&&x.pathname!=="/messages"&&/(mensagen|message|conversa|conversation|chat)/.test(x.pathname)),b);
  const seen=new Set;
  return rows.map(x=>({id:x.id,url:new URL(x.url,b).toString(),title:x.title,listingUrl:"",listingTitle:x.title,buyerName:"",lastMessage:"",lastMessageId:"",lastMessageAt:"",unread:false})).filter(x=>!seen.has(x.url)&&seen.add(x.url)).slice(0,100);
}
function stableMessageId(row){return row.id||crypto.createHash("sha256").update(`${row.incoming?"in":"out"}|${row.timestamp}|${row.text}`).digest("hex").slice(0,24)}
async function messages(page,url){
  await page.goto(url,{waitUntil:"domcontentloaded",timeout:TIMEOUT});
  await cookies(page);
  await page.waitForTimeout(800);
  const rows=await page.locator('article,[data-message-id],[data-testid*="message" i],[class*="message" i],[class*="bubble" i]').evaluateAll(ns=>{
    const visible=n=>{const r=n.getBoundingClientRect(),s=getComputedStyle(n);return r.width>20&&r.height>10&&s.display!=="none"&&s.visibility!=="hidden"};
    const candidates=ns.filter(visible).filter(n=>!Array.from(n.children).some(c=>c.matches?.('article,[data-message-id],[data-testid*="message" i],[class*="message" i],[class*="bubble" i]')&&visible(c)));
    return candidates.map(n=>{
      const text=(n.innerText||n.textContent||"").trim().replace(/\s+/g," ");
      const meta=`${n.className||""} ${n.getAttribute("data-direction")||""} ${n.getAttribute("data-testid")||""} ${n.getAttribute("aria-label")||""}`.toLowerCase();
      const rect=n.getBoundingClientRect(),style=getComputedStyle(n);
      let incoming=true;
      if(/outgoing|sent|self|mine|own|justify-end|items-end|right/.test(meta)||style.alignSelf==="flex-end")incoming=false;
      else if(/incoming|received|other|justify-start|items-start|left/.test(meta)||style.alignSelf==="flex-start")incoming=true;
      else if(rect.width<innerWidth*.82)incoming=(rect.left+rect.width/2)<innerWidth/2;
      return{id:n.getAttribute("data-message-id")||n.getAttribute("data-id")||"",sender:n.getAttribute("data-sender")||n.querySelector('[data-sender], [class*="sender" i]')?.textContent?.trim()||"",text,timestamp:n.querySelector("time")?.getAttribute("datetime")||n.getAttribute("data-timestamp")||"",incoming};
    }).filter(x=>x.text&&x.text.length<4000);
  });
  const seen=new Set;
  return rows.map(x=>({...x,id:stableMessageId(x),conversationId:url})).filter(x=>{const k=`${x.incoming}|${x.timestamp}|${x.text}`;if(seen.has(k))return false;seen.add(k);return true}).slice(-100);
}
async function visible(page,selectors){for(const q of selectors){const all=page.locator(q);const count=await all.count();for(let i=0;i<count;i++){const x=all.nth(i);if(await x.isVisible().catch(()=>false))return x}}return null}
async function send(page,url,text){
  await page.goto(url,{waitUntil:"domcontentloaded",timeout:TIMEOUT});
  await cookies(page);
  const fieldSelectors=['textarea','[contenteditable="true"]','input[name*="message" i]','textarea[name*="message" i]'];
  let field=await visible(page,fieldSelectors);
  if(!field){
    const contact=await visible(page,['button:has-text("mensagem")','a:has-text("mensagem")','button:has-text("Contactar")','a:has-text("Contactar")','button:has-text("Enviar mensagem")','a:has-text("Enviar mensagem")']);
    if(!contact)throw Error("CustoJusto contact button was not found on this listing");
    await contact.click({noWaitAfter:true,timeout:15000});await page.waitForTimeout(700);
    field=await visible(page,fieldSelectors);
    if(!field){await page.waitForSelector(fieldSelectors.join(','),{state:"visible",timeout:20000});field=await visible(page,fieldSelectors)}
  }
  await field.fill(text,{timeout:15000});
  const form=field.locator('xpath=ancestor::form[1]');
  let submit=null;
  if(await form.count())submit=await visible(form,['button[type="submit"]','button:has-text("Enviar")','button:has-text("Send")','input[type="submit"]']);
  if(!submit)throw Error("CustoJusto send button was not found inside the message form");
  const normalized=text.trim().replace(/\s+/g," ");
  const encoded=encodeURIComponent(text).replace(/%20/g,'+');
  const mutation=page.waitForResponse(r=>{
    const method=r.request().method(),data=r.request().postData()||"";
    if(!["POST","PUT","PATCH"].includes(method))return false;
    let decoded=data;try{decoded=decodeURIComponent(data.replace(/\+/g," "))}catch{}
    return data.includes(text)||data.includes(encoded)||decoded.includes(text);
  },{timeout:20000}).catch(()=>null);
  await submit.click({noWaitAfter:true,timeout:15000});
  let proof="";
  for(let attempt=0;attempt<40&&!proof;attempt++){
    await page.waitForTimeout(250);
    const response=await Promise.race([mutation,Promise.resolve(null)]);
    if(response&&response.status()>=400)throw Error(`CustoJusto rejected the message with HTTP ${response.status()}`);
    if(response&&response.status()>=200&&response.status()<300)proof="network";
    const successToast=await page.getByText(/mensagem enviada|message sent|enviado com sucesso/i).first().isVisible().catch(()=>false);
    if(successToast)proof="toast";
    const currentValue=await field.evaluate(el=>el.isContentEditable?(el.textContent||""):String(el.value||"")).catch(()=>text);
    if(currentValue.trim()==="")proof="form-cleared";
    const found=await page.locator('article,[data-message-id],[data-testid*="message" i],[class*="message" i],[class*="bubble" i]').evaluateAll((nodes,expected)=>nodes.some(n=>{
      const r=n.getBoundingClientRect(),s=getComputedStyle(n),actual=(n.innerText||n.textContent||"").trim().replace(/\s+/g," ");
      return r.width>20&&r.height>10&&s.display!=="none"&&s.visibility!=="hidden"&&actual===expected;
    }),normalized).catch(()=>false);
    if(found)proof="conversation";
  }
  if(!proof)throw Error("CustoJusto did not confirm delivery; message was not marked as sent");
  return{ok:true,verified:true,proof,url:page.url()};
}

app.get("/health",(_,res)=>res.json({ok:true,activeProfiles:contexts.size}));
app.post("/manual/prepare",auth,async(req,res)=>{try{const account=id(req.body?.accountId),email=String(req.body?.email||"").trim(),password=String(req.body?.password||"");if(!email||!password)return res.status(400).json({error:"email and password are required"});const s=await session(account);await s.page.goto(new URL("/login",base(req.body?.baseUrl)).toString(),{waitUntil:"domcontentloaded",timeout:TIMEOUT});await cookies(s.page);const emailField=s.page.locator("#username");const passwordField=s.page.locator("#password");await emailField.waitFor({state:"visible"});await emailField.fill(email);await passwordField.waitFor({state:"visible"});await passwordField.fill(password);res.json({ok:true})}catch(e){res.status(500).json({error:e.message})}});
app.get("/manual/open",async(req,res)=>{try{const s=await session(req.query.accountId);await s.page.goto(new URL("/login",base(req.query.baseUrl)).toString(),{waitUntil:"domcontentloaded",timeout:TIMEOUT});await cookies(s.page);const mobile=String(req.query.mobile||"")==="1";res.redirect(302,mobile?"/vnc.html?autoconnect=true&resize=scale&show_dot=true":"/vnc.html?autoconnect=true&resize=remote")}catch(e){res.status(500).type("text/plain").send(`Unable to open browser: ${e.message}`)}});
app.post("/status",auth,(req,res)=>use(req,res,async(p,b)=>{if(p.url()==="about:blank")await p.goto(b,{waitUntil:"domcontentloaded",timeout:TIMEOUT});res.json({ok:true,loggedIn:await logged(p),url:p.url()})}));
app.post("/conversations",auth,(req,res)=>use(req,res,async(p,b)=>{if(!await logged(p))return res.status(401).json({error:"CustoJusto session is not logged in"});res.json(await conversations(p,b))}));
app.post("/messages",auth,(req,res)=>use(req,res,async(p)=>{if(!await logged(p))return res.status(401).json({error:"CustoJusto session is not logged in"});const u=String(req.body?.conversationUrl||"");if(!u)return res.status(400).json({error:"conversationUrl is required"});res.json(await messages(p,u))}));
app.post("/send",auth,(req,res)=>use(req,res,async(p)=>{if(!await logged(p))return res.status(401).json({error:"CustoJusto session is not logged in"});const u=String(req.body?.conversationUrl||req.body?.listingUrl||""),t=String(req.body?.text||req.body?.message||"");if(!u||!t)return res.status(400).json({error:"conversationUrl and text are required"});res.json(await send(p,u,t))}));
app.post("/listing",auth,async(req,res)=>{const u=String(req.body?.listingUrl||"");if(!u)return res.status(400).json({error:"listingUrl is required"});try{const r=await fetch(u),h=await r.text();res.json({url:u,title:((h.match(/<title[^>]*>([^<]*)<\/title>/i)||[,""])[1]).trim(),price:"",sellerName:"",location:""})}catch(e){res.status(500).json({error:e.message})}});
app.listen(PORT,"127.0.0.1",()=>console.log(`CustoJusto session worker listening on ${PORT}`));
