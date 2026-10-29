import {MANAGEMENT_STATUS} from './management-protocol.js';

function checkAbort(signal) {
  if (signal?.aborted) throw signal.reason ?? new DOMException('Configuration operation cancelled', 'AbortError');
}

function pause(ms, signal) {
  return new Promise((resolve, reject) => {
    const abort = () => {
      clearTimeout(timer);
      signal?.removeEventListener('abort', abort);
      reject(signal.reason ?? new DOMException('Configuration operation cancelled', 'AbortError'));
    };
    const timer = setTimeout(() => { signal?.removeEventListener('abort', abort); resolve(); }, ms);
    signal?.addEventListener('abort', abort, {once: true});
    if (signal?.aborted) abort();
  });
}

// A received Busy did not complete the requested phase. Transport failures and
// all other statuses remain visible to the caller without automatic replay.
export function configurationReadySession(session, {signal, readyPollMs = 30, readyTimeoutMs = 15000} = {}) {
  if (!Number.isFinite(readyPollMs) || readyPollMs < 0 || readyPollMs > 1000 ||
      !Number.isFinite(readyTimeoutMs) || readyTimeoutMs <= 0 || readyTimeoutMs > 60000) throw Error('Invalid configuration readiness budget');
  return {async request(...args) {
    const deadline = performance.now() + readyTimeoutMs;
    for (;;) {
      checkAbort(signal);
      try {
        const result = await session.request(...args);
        checkAbort(signal);
        return result;
      } catch (error) {
        checkAbort(signal);
        if (error.code !== MANAGEMENT_STATUS.BUSY && error.code !== 'BUSY') throw error;
        const remaining = deadline - performance.now();
        if (remaining <= 0) throw Error('Configuration readiness timed out; operation state retained for recovery');
        await pause(Math.min(readyPollMs, remaining), signal);
      }
    }
  }};
}
