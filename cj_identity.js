const crypto = require('crypto');

function cleanText(value) {
  return String(value || '').trim().replace(/\s+/g, ' ');
}

function canonicalConversationUrl(value, base) {
  const url = new URL(String(value || ''), base);
  url.hash = '';
  for (const key of [...url.searchParams.keys()]) {
    if (/^(utm_|fbclid$|gclid$|ref$|source$)/i.test(key)) url.searchParams.delete(key);
  }
  url.searchParams.sort();
  return url.toString();
}

function messageCanonical(message) {
  return [
    message.incoming ? 'in' : 'out',
    cleanText(message.sender).toLowerCase(),
    cleanText(message.timestamp),
    cleanText(message.text),
  ].join('|');
}

function stableMessageId(conversationUrl, message) {
  const nativeId = cleanText(message.id);
  if (nativeId) return nativeId;
  return crypto.createHash('sha256')
    .update(`${canonicalConversationUrl(conversationUrl)}|${messageCanonical(message)}`)
    .digest('hex')
    .slice(0, 32);
}

function deduplicateMessages(conversationUrl, messages) {
  const seen = new Set();
  const output = [];
  for (const message of messages) {
    const id = stableMessageId(conversationUrl, message);
    if (seen.has(id)) continue;
    seen.add(id);
    output.push({ ...message, id, conversationId: canonicalConversationUrl(conversationUrl) });
  }
  return output;
}

module.exports = { cleanText, canonicalConversationUrl, messageCanonical, stableMessageId, deduplicateMessages };
