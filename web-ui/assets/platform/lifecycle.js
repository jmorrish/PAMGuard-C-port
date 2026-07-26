(() => {
  "use strict";

  const eventTarget = globalThis.EventTarget?.prototype;
  if (!eventTarget) {
    throw new Error("EventTarget is required by the PAMGuard web shell");
  }
  const originalAdd = eventTarget.addEventListener;
  const originalRemove = eventTarget.removeEventListener;
  const listeners = [];
  let capturing = true;
  let disposed = false;

  eventTarget.addEventListener = function addTrackedListener(
    type,
    listener,
    options) {
    if (capturing && listener !== null) {
      listeners.push({
        target: this,
        type,
        listener,
        capture: typeof options === "boolean"
          ? options
          : Boolean(options?.capture)
      });
    }
    return originalAdd.call(this, type, listener, options);
  };

  function seal() {
    if (!capturing) return;
    capturing = false;
    eventTarget.addEventListener = originalAdd;
  }

  function dispose() {
    if (disposed) return;
    disposed = true;
    seal();
    for (const registration of listeners.reverse()) {
      originalRemove.call(
        registration.target,
        registration.type,
        registration.listener,
        registration.capture);
    }
    listeners.length = 0;
  }

  const platform = globalThis.PamguardPlatform || {};
  globalThis.PamguardPlatform = Object.freeze({
    ...platform,
    lifecycle: Object.freeze({
      seal,
      dispose,
      get listenerCount() {
        return listeners.length;
      }
    })
  });
})();
