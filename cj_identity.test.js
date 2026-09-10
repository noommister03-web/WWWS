const test = require('node:test');
const assert = require('node:assert/strict');
const { canonicalConversationUrl, stableMessageId, deduplicateMessages } = require('./cj_identity');

test('conversation URLs are canonical across tracking and fragments', () => {
  assert.equal(
    canonicalConversationUrl('/mensagens/42?utm_source=x&thread=7#bottom', 'https://www.custojusto.pt'),
    'https://www.custojusto.pt/mensagens/42?thread=7'
  );
});

test('fallback message ID remains stable when newer duplicate text is appended', () => {
  const url = 'https://www.custojusto.pt/mensagens/42';
  const first = { id: '', incoming: true, sender: 'seller', timestamp: '2026-09-10T10:00:00Z', text: 'Sim' };
  const idBefore = stableMessageId(url, first);
  const rows = deduplicateMessages(url, [first, { ...first }, { ...first, timestamp: '2026-09-10T10:01:00Z' }]);
  assert.equal(rows[0].id, idBefore);
  assert.equal(rows.length, 2);
});

test('native platform IDs are preserved', () => {
  assert.equal(stableMessageId('https://www.custojusto.pt/mensagens/42', { id: 'abc-123' }), 'abc-123');
});
