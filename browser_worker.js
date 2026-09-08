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
async function session(account){const key=id(account);if(contexts.has(key))return contexts.get(key);const profile=path.join(ROOT,key);await fs.mkdir(profile,{recursive:true});const context=await chromium.launchPersistentContext(profile,{headless:false,viewport:{width:1365,height:900},timeout:Number(process.env.CJ_BROWSER_LAUNCH_TIMEOUT_MS||120000),args:["--no-sandbox","--disable-dev-shm-usage","--start-maximized"]});const page=context.pages()[0]||await context.newPage();page.setDefaultTimeout(TIMEOUT);const s={context,page,queue:Promise.resolve()};contexts.set(key,s);context.on("close",()=>contexts.delete(key));return s}
async function exclusive(s,work){const previous=s.queue.catch(()=>{});let release;s.queue=new Promise(resolve=>{release=resolve});await previous;try{return await work()}finally{release()}}
async function cookies(page){for(const q of ["#CybotCookiebotDialogBodyButtonDecline","#CybotCookiebotDialogBodyLevelButtonLevelOptinAllowAll","button:has-text('Aceitar e fechar')"]){const b=page.locator(q).first();if(await b.isVisible().catch(()=>false)){await b.click().catch(()=>{});return}}}
async function logged(page){if(/\/login|\/entrar|signin/i.test(page.url()))return false;if(await page.locator('a[href*="login"],a[href*="entrar"],button:has-text("Entrar")').first().isVisible().catch(()=>false))return false;return(await page.locator('a[href*="conta"],a[href*="account"],a[href*="mensagens"],a[href*="messages"]').count())>0}
async function use(req,res,fn){try{const s=await session(req.body?.accountId);await exclusive(s,()=>fn(s.page,base(req.body?.baseUrl)))}catch(e){if(!res.headersSent)res.status(Number(e.status)||500).json({error:e.message,status:Number(e.status)||500})}}
async function conversations(page,b){
  await page.goto(new URL("/mensagens",b).toString(),{waitUntil:"domcontentloaded",timeout:TIMEOUT});
  await cookies(page);
  await page.waitForTimeout(1500);
  let previousConversationCount=-1,stableConversationPasses=0;
  for(let pass=0;pass<40&&stableConversationPasses<3;pass++){
    const count=await page.locator('a[href],[data-conversation-id],[data-chat-id],[data-testid*="conversation" i],[data-testid*="chat" i]').count();
    stableConversationPasses=count===previousConversationCount?stableConversationPasses+1:0;
    previousConversationCount=count;
    await page.evaluate(()=>window.scrollTo(0,document.body.scrollHeight));
    await page.waitForTimeout(500);
  }
  const rows=await page.locator('a[href],[data-conversation-id],[data-chat-id],[data-testid*="conversation" i],[data-testid*="chat" i]').evaluateAll((nodes,origin)=>nodes.map((n,i)=>{
    const anchor=n.matches('a[href]')?n:n.closest('a[href]');
    const raw=anchor?.getAttribute("href")||n.getAttribute("data-url")||n.getAttribute("data-href")||"";
    let u;try{u=new URL(raw,origin)}catch{return null}
    const text=(n.innerText||n.textContent||anchor?.innerText||"").trim().replace(/\s+/g," ");
    const marker=`${n.getAttribute("data-conversation-id")||""} ${n.getAttribute("data-chat-id")||""} ${n.getAttribute("data-testid")||""} ${n.className||""}`.toLowerCase();
    const route=`${u.pathname}${u.search}${u.hash}`.toLowerCase();
    const isInboxRoot=/^\/(mensagens|messages)\/?$/.test(u.pathname.toLowerCase())&&!u.search&&!u.hash;
    const looksLikeThread=!isInboxRoot&&(/(mensagen|message|conversa|conversation|chat)/.test(route)||/(conversation|chat|thread|message)/.test(marker));
    if(!looksLikeThread)return null;
    return{id:n.getAttribute("data-conversation-id")||n.getAttribute("data-chat-id")||n.getAttribute("data-id")||`conversation-${i}`,url:u.toString(),title:text||"Диалог"};
  }).filter(Boolean),b);
  const seen=new Set;
  return rows.map(x=>({id:x.id,url:x.url,title:x.title,listingUrl:"",listingTitle:x.title,buyerName:"",lastMessage:"",lastMessageId:"",lastMessageAt:"",unread:false})).filter(x=>!seen.has(x.url)&&seen.add(x.url)).slice(0,100);
}
function stableMessageId(row){return row.id||crypto.createHash("sha256").update(`${row.incoming?"in":"out"}|${row.timestamp}|${row.text}`).digest("hex").slice(0,24)}
async function messages(page,url){
  await page.goto(url,{waitUntil:"domcontentloaded",timeout:TIMEOUT});
  await cookies(page);
  await page.waitForTimeout(1500);
  const selector='article,[data-message-id],[data-testid*="message" i],[data-testid*="bubble" i],[class*="chat-message" i],[class*="message-bubble" i],[class*="messageItem" i],[class*="message-item" i],[class*="bubble" i]';
  let previousMessageCount=-1,stableMessagePasses=0;
  for(let pass=0;pass<80&&stableMessagePasses<4;pass++){
    const count=await page.locator(selector).count();
    stableMessagePasses=count===previousMessageCount?stableMessagePasses+1:0;
    previousMessageCount=count;
    await page.evaluate(()=>{const candidates=[...document.querySelectorAll('*')].filter(e=>{const s=getComputedStyle(e);return e.scrollHeight>e.clientHeight+50&&/(auto|scroll)/.test(s.overflowY)}).sort((a,b)=>b.clientHeight-a.clientHeight);(candidates[0]||document.scrollingElement).scrollTop=0;window.scrollTo(0,0)});
    await page.waitForTimeout(500);
  }
  const rows=await page.locator(selector).evaluateAll((ns,selector)=>{
    const visible=n=>{const r=n.getBoundingClientRect(),s=getComputedStyle(n);return r.width>20&&r.height>10&&s.display!=="none"&&s.visibility!=="hidden"};
    const candidates=ns.filter(visible).filter(n=>!Array.from(n.children).some(c=>c.matches?.(selector)&&visible(c)));
    return candidates.map(n=>{
      const clone=n.cloneNode(true);clone.querySelectorAll('button,svg,[aria-hidden="true"]').forEach(x=>x.remove());
      const text=(clone.innerText||clone.textContent||"").trim().replace(/\s+/g," ");
      let p=n,meta="";for(let i=0;p&&i<4;i++,p=p.parentElement)meta+=` ${p.className||""} ${p.getAttribute?.("data-direction")||""} ${p.getAttribute?.("data-testid")||""} ${p.getAttribute?.("aria-label")||""}`;
      meta=meta.toLowerCase();const rect=n.getBoundingClientRect(),style=getComputedStyle(n);
      let incoming=true;
      if(/outgoing|sent|self|mine|own|from-me|message--right|justify-end|items-end/.test(meta)||style.alignSelf==="flex-end")incoming=false;
      else if(/incoming|received|other|from-them|message--left|justify-start|items-start/.test(meta)||style.alignSelf==="flex-start")incoming=true;
      else incoming=(rect.left+rect.width/2)<innerWidth/2;
      return{id:n.getAttribute("data-message-id")||n.getAttribute("data-id")||"",sender:n.getAttribute("data-sender")||n.querySelector('[data-sender],[class*="sender" i],[class*="author" i]')?.textContent?.trim()||"",text,timestamp:n.querySelector("time")?.getAttribute("datetime")||n.getAttribute("data-timestamp")||"",incoming};
    }).filter(x=>x.text&&x.text.length<4000);
  },selector);
  const seen=new Set;
  return rows.map(x=>({...x,id:stableMessageId(x),conversationId:url})).filter(x=>{const k=`${x.incoming}|${x.timestamp}|${x.text}`;if(seen.has(k))return false;seen.add(k);return true});
}
async function visible(page,selectors){for(const q of selectors){const all=page.locator(q);const count=await all.count();for(let i=0;i<count;i++){const x=all.nth(i);if(await x.isVisible().catch(()=>false))return x}}return null}
async function send(page,url,text){
  await page.goto(url,{waitUntil:"domcontentloaded",timeout:TIMEOUT});
  await cookies(page);
  await page.waitForTimeout(1500);
  const fieldSelectors=['textarea[name*="message" i]','textarea[placeholder*="mensagem" i]','[contenteditable="true"][role="textbox"]','textarea','[role="textbox"]'];
  let field=await visible(page,fieldSelectors);
  if(!field){
    const contact=await visible(page,['button:has-text("Enviar mensagem")','a:has-text("Enviar mensagem")','button:has-text("Mensagem")','a:has-text("Mensagem")','button:has-text("Contactar")','a:has-text("Contactar")','[data-testid*="contact" i]']);
    if(!contact)throw Error("CustoJusto contact button was not found on this listing");
    await contact.scrollIntoViewIfNeeded();
    await contact.click({timeout:15000});
    await page.waitForTimeout(1500);
    field=await visible(page,fieldSelectors);
  }
  if(!field)throw Error("CustoJusto message field was not found");
  await field.scrollIntoViewIfNeeded();
  await field.click({timeout:15000});
  await field.press(process.platform==="darwin"?"Meta+A":"Control+A").catch(()=>{});
  await field.press("Backspace").catch(()=>{});
  await field.type(text,{delay:35,timeout:TIMEOUT});
  await page.waitForTimeout(1200);
  const composer=field.locator('xpath=ancestor::*[self::form or @role="dialog" or contains(@class,"message") or contains(@class,"contact")][1]');
  const submitSelectors=['button[type="submit"]:not([disabled])','button:has-text("Enviar"):not([disabled])','button[aria-label*="enviar" i]:not([disabled])','button[title*="enviar" i]:not([disabled])','[role="button"][aria-label*="enviar" i]','[data-testid*="send" i]:not([disabled])'];
  let submit=null;
  if(await composer.count())submit=await visible(composer,submitSelectors);
  if(!submit)submit=await visible(page,submitSelectors);
  if(!submit)throw Error("CustoJusto enabled send button was not found");
  const deliveryResponse=page.waitForResponse(response=>{
    const request=response.request();
    if(!["POST","PUT","PATCH"].includes(request.method()))return false;
    const data=request.postData()||"";
    let decoded=data;
    try{decoded=decodeURIComponent(data.replace(/\+/g," "))}catch{}
    return data.includes(text)||decoded.includes(text)||decoded.includes(text.trim());
  },{timeout:20000}).catch(()=>null);
  await submit.scrollIntoViewIfNeeded();
  await submit.click({timeout:15000});
  const response=await deliveryResponse;
  if(response&&response.status()>=400){
    const detail=(await response.text().catch(()=>"")).slice(0,500);
    const e=Error(`CustoJusto rejected the message with HTTP ${response.status()}${detail?`: ${detail}`:""}`);
    e.status=response.status();
    throw e;
  }
  const normalized=text.trim().replace(/\s+/g," ");
  let proof=response&&response.status()>=200&&response.status()<300?"network":"";
  for(let attempt=0;attempt<40&&!proof;attempt++){
    await page.waitForTimeout(250);
    const successToast=await page.getByText(/mensagem enviada|message sent|enviado com sucesso/i).first().isVisible().catch(()=>false);
    if(successToast)proof="toast";
    const currentValue=await field.evaluate(el=>el.isContentEditable?(el.textContent||""):String(el.value||"")).catch(()=>text);
    if(currentValue.trim()==="")proof="form-cleared";
    const found=await page.locator('article,[data-message-id],[data-testid*="message" i],[class*="message" i],[class*="bubble" i]').evaluateAll((nodes,expected)=>nodes.some(n=>{
      const r=n.getBoundingClientRect(),style=getComputedStyle(n),actual=(n.innerText||n.textContent||"").trim().replace(/\s+/g," ");
      return r.width>20&&r.height>10&&style.display!=="none"&&style.visibility!=="hidden"&&actual.includes(expected);
    }),normalized).catch(()=>false);
    if(found)proof="conversation";
  }
  return{ok:true,verified:Boolean(proof),proof:proof||"submitted-unverified",url:page.url()};
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
