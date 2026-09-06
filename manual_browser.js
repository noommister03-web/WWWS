const { chromium } = require("playwright");
const fs = require("fs/promises");
const path = require("path");

const BASE = process.env.CJ_BASE_URL || "https://www.custojusto.pt";
const PROFILE_ROOT = process.env.CJ_PROFILE_ROOT || "/app/data/custojusto/profiles";
const ACCOUNT_ID = String(process.env.CJ_MANUAL_ACCOUNT_ID || "1").trim();
const DISPLAY = process.env.DISPLAY || ":99";

if (!/^[A-Za-z0-9_-]{1,100}$/.test(ACCOUNT_ID)) {
  throw new Error("CJ_MANUAL_ACCOUNT_ID must contain only letters, digits, _ or -");
}

async function main() {
  const profile = path.join(PROFILE_ROOT, ACCOUNT_ID);
  await fs.mkdir(profile, { recursive: true });

  // The browser is intentionally headed: the account owner completes
  // Cloudflare Turnstile and login themselves through the protected noVNC UI.
  const context = await chromium.launchPersistentContext(profile, {
    headless: false,
    viewport: { width: 1365, height: 900 },
    locale: "pt-PT",
    timezoneId: "Europe/Lisbon",
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--start-maximized"]
  });

  const page = context.pages()[0] || await context.newPage();
  page.setDefaultTimeout(60000);
  await page.goto(new URL("/login", BASE).toString(), {
    waitUntil: "domcontentloaded",
    timeout: 60000
  });

  console.log(`Manual CustoJusto browser ready for account ${ACCOUNT_ID}`);
  console.log("Complete login and Turnstile in the protected noVNC page. The profile is saved on /app/data.");

  const shutdown = async () => {
    try { await context.close(); } catch (_) {}
    process.exit(0);
  };
  process.on("SIGTERM", shutdown);
  process.on("SIGINT", shutdown);
}

main().catch(error => {
  console.error("Manual CustoJusto browser failed:", error);
  process.exit(1);
});
