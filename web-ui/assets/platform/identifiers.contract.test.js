"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");

globalThis.PamguardPlatform = {};
require("./identifiers.js");

const { uuidV4 } = globalThis.PamguardPlatform.identifiers;
const UUID_V4 =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

test("uses native randomUUID when the context provides it", () => {
  const expected = "01234567-89ab-4cde-8fab-0123456789ab";
  assert.equal(
    uuidV4({ randomUUID: () => expected }),
    expected);
});

test("builds RFC 4122 UUIDv4 identities without randomUUID", () => {
  const value = uuidV4({
    getRandomValues(bytes) {
      for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = index * 17;
      }
      return bytes;
    }
  });
  assert.match(value, UUID_V4);
  assert.equal(value[14], "4");
  assert.match(value[19], /[89ab]/);
});

test("fails closed when secure random values are unavailable", () => {
  assert.throws(
    () => uuidV4({}),
    /Secure random values are required/);
});
