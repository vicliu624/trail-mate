import assert from 'node:assert/strict';
import {configurationReadySession} from '../src/config-ready-request.js';

let attempts = 0;
const ready = configurationReadySession({async request(operation, body) {
  assert.equal(operation, 17); assert.deepEqual(body, [0, 320, null]);
  if (++attempts < 3) throw Object.assign(Error('not ready'), {code: 5});
  return 'snapshot';
}}, {readyPollMs: 0});
assert.equal(await ready.request(17, [0, 320, null]), 'snapshot');
assert.equal(attempts, 3);

for (const error of [Object.assign(Error('conflict'), {code: 4}), Error('transport disconnected')]) {
  attempts = 0;
  const session = configurationReadySession({async request() { ++attempts; throw error; }}, {readyPollMs: 0});
  await assert.rejects(session.request(21, []), actual => actual === error);
  assert.equal(attempts, 1);
}
const cancelled = new AbortController();
cancelled.abort(Error('cancelled'));
await assert.rejects(configurationReadySession({request() { assert.fail('aborted request was sent'); }}, {signal: cancelled.signal}).request(17, []), /cancelled/);
const duringWait = new AbortController();
attempts = 0;
const waiting = configurationReadySession({async request() { ++attempts; throw Object.assign(Error('busy'), {code: 5}); }}, {signal: duringWait.signal, readyPollMs: 1000}).request(17, []);
setTimeout(() => duringWait.abort(Error('stop waiting')), 5);
await assert.rejects(waiting, /stop waiting/);
assert.equal(attempts, 1);
await assert.rejects(configurationReadySession({async request() { throw Object.assign(Error('busy'), {code: 5}); }}, {readyPollMs: 0, readyTimeoutMs: 5}).request(21, []), /timed out/);
console.log('Configuration readiness: Busy-only bounded wait, cancellation and preserved transport/conflict failures passed');
