// One timer per open page. Background tabs wait for resume instead of polling;
// foreground requests wait for user operations and never overlap one another.
export function createLiveRefresh({refresh, canRefresh, isVisible,
  setTimer = setTimeout, clearTimer = clearTimeout, interval = 60000, busyDelay = 5000}) {
  let ready = false, stopped = false, running = false, timer = null;
  const cancel = () => { if (timer !== null) clearTimer(timer); timer = null; };
  const schedule = delay => {
    cancel();
    if (ready && !stopped && !running && isVisible()) timer = setTimer(run, delay);
  };
  async function run() {
    timer = null;
    if (!ready || stopped || running || !isVisible()) return;
    if (!canRefresh()) { schedule(busyDelay); return; }
    running = true;
    try { await refresh(); }
    catch { /* The page reports the live query error; retry remains bounded. */ }
    finally { running = false; schedule(interval); }
  }
  return {
    setReady(value) { ready = value; schedule(interval); },
    touch() { schedule(interval); },
    resume() { schedule(0); },
    stop() { stopped = true; cancel(); },
  };
}
