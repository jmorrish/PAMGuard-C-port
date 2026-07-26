"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");

globalThis.PamguardPlatform = {};
require("./project-client.js");

const {
  ProjectConflictError,
  ProjectProtocolError,
  ProjectStateError,
  createProjectClient
} = globalThis.PamguardPlatform.project;

const PROJECT_ONE =
  "11111111-1111-4111-8111-111111111111";
const PROJECT_TWO =
  "22222222-2222-4222-8222-222222222222";
const UNIT_ONE =
  "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
const ETAG_ONE = `"pgp1-${"A".repeat(43)}"`;
const ETAG_TWO = `"pgp1-${"B".repeat(43)}"`;
const ETAG_THREE = `"pgp1-${"C".repeat(43)}"`;
const ETAG_FOUR = `"pgp1-${"D".repeat(43)}"`;
const ETAG_FIVE = `"pgp1-${"E".repeat(43)}"`;
const ETAG_SIX = `"pgp1-${"F".repeat(43)}"`;

function activeSnapshot(etag, projectId = PROJECT_ONE) {
  return {
    schemaVersion: 1,
    project: {
      schemaVersion: 1,
      projectId
    },
    workingRevision: 0,
    savedRevision: null,
    authorityRevision: 0,
    workingContentHash: `sha256:${"0".repeat(64)}`,
    savedContentHash: null,
    dirty: true,
    etag,
    projection: {
      status: "runnable",
      issues: []
    }
  };
}

function jsonResponse(body, {
  status = 200,
  etag = null,
  location = null
} = {}) {
  const headers = {
    "Content-Type": "application/json"
  };
  if (etag !== null) headers.ETag = etag;
  if (location !== null) headers.Location = location;
  return new Response(JSON.stringify(body), {
    status,
    headers
  });
}

function mockFetch(steps) {
  const calls = [];
  const fetchImpl = async (url, options) => {
    const call = { url: String(url), options };
    calls.push(call);
    const step = steps.shift();
    assert.ok(step, `Unexpected request to ${url}`);
    if (step.check) step.check(call);
    return step.response;
  };
  return { calls, fetchImpl, steps };
}

function clientFor(mock, options = {}) {
  return createProjectClient({
    getBaseUrl: () => "https://engine.test/",
    getApiKey: () => "test-key",
    fetchImpl: mock.fetchImpl,
    ...options
  });
}

test("loads every project read surface and adopts only active ETags", async () => {
  const savedEnvelope = {
    fileFormat: "pamguard-project",
    fileFormatVersion: 1,
    canonicalizationVersion: 1,
    authorityRevision: 1,
    savedRevision: 1,
    contentHash: `sha256:${"1".repeat(64)}`,
    savedAtUnixMs: 1,
    project: {
      schemaVersion: 1,
      projectId: PROJECT_ONE
    }
  };
  const mock = mockFetch([
    {
      response: jsonResponse({
        schemaVersion: 1,
        descriptorSet: {},
        controlledUnitTypes: [],
        displayProviderTypes: []
      })
    },
    {
      response: jsonResponse({
        schemaVersion: 1,
        projects: []
      })
    },
    {
      response: jsonResponse(
        savedEnvelope,
        { etag: ETAG_SIX })
    },
    {
      response: jsonResponse(
        activeSnapshot(ETAG_ONE),
        { etag: ETAG_ONE })
    },
    {
      response: jsonResponse({
        schemaVersion: 1,
        projectId: PROJECT_ONE,
        workingRevision: 0,
        authorityRevision: 0,
        projection: {}
      }, { etag: ETAG_ONE })
    },
    {
      check(call) {
        assert.match(
          call.url,
          /unitId=aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa/);
        assert.match(call.url, /inputRole=rawAudio/);
      },
      response: jsonResponse({
        schemaVersion: 1,
        target: {
          unitId: UNIT_ONE,
          inputRole: "rawAudio"
        },
        sources: []
      }, { etag: ETAG_ONE })
    }
  ]);
  const client = clientFor(mock);

  await client.loadCatalogue();
  await client.listProjects();
  assert.deepEqual(
    await client.loadSavedProject(PROJECT_ONE),
    savedEnvelope);
  assert.equal(
    client.activeEtag,
    null,
    "durable-project ETag became the active authority token");
  await client.loadActive();
  await client.loadInspection();
  await client.loadCompatibleSources(
    UNIT_ONE,
    "rawAudio");

  assert.equal(client.activeEtag, ETAG_ONE);
  assert.equal(mock.calls.length, 6);
  for (const call of mock.calls) {
    assert.equal(
      call.options.headers["X-API-Key"],
      "test-key");
    assert.equal(
      call.options.headers.Accept,
      "application/json");
  }
  assert.equal(mock.steps.length, 0);
});

test("sends exact schema-v1 commands with the current If-Match", async () => {
  const checks = [
    {
      path: "/v1/projects/active/mutations",
      expectedEtag: ETAG_ONE,
      expectedBody: {
        schemaVersion: 1,
        validateOnly: false,
        operations: [{
          op: "addControlledUnit",
          clientRef: "fft",
          typeId: "pamguard.fft",
          name: null,
          dependencyPolicy: "add-defaults"
        }]
      }
    },
    {
      path: "/v1/projects/active/new",
      expectedEtag: ETAG_TWO,
      expectedBody: {
        schemaVersion: 1,
        name: "Fresh",
        description: "",
        discardDirty: true
      }
    },
    {
      path: "/v1/projects/active/open",
      expectedEtag: ETAG_THREE,
      expectedBody: {
        schemaVersion: 1,
        projectId: PROJECT_ONE,
        discardDirty: false
      }
    },
    {
      path: "/v1/projects/active/save",
      expectedEtag: ETAG_FOUR,
      expectedBody: undefined
    },
    {
      path: "/v1/projects/active/save-as",
      expectedEtag: ETAG_FIVE,
      expectedBody: {
        schemaVersion: 1,
        name: "Saved copy"
      }
    }
  ];
  const responseEtags = [
    ETAG_TWO,
    ETAG_THREE,
    ETAG_FOUR,
    ETAG_FIVE,
    ETAG_SIX
  ];
  const mock = mockFetch(checks.map((check, index) => ({
    check(call) {
      assert.ok(call.url.endsWith(check.path));
      assert.equal(call.options.method, "POST");
      assert.equal(
        call.options.headers["If-Match"],
        check.expectedEtag);
      if (check.expectedBody === undefined) {
        assert.equal(call.options.body, undefined);
        assert.equal(
          call.options.headers["Content-Type"],
          undefined);
      }
      else {
        assert.deepEqual(
          JSON.parse(call.options.body),
          check.expectedBody);
        assert.equal(
          call.options.headers["Content-Type"],
          "application/json");
      }
    },
    response: jsonResponse(
      index === 0
        ? {
            schemaVersion: 1,
            changed: true,
            validatedOnly: false,
            createdEntities: [],
            active: activeSnapshot(responseEtags[index])
          }
        : activeSnapshot(
            responseEtags[index],
            index === 4 ? PROJECT_TWO : PROJECT_ONE),
      {
        status: index === 4 ? 201 : 200,
        etag: responseEtags[index],
        location: index === 4
          ? `/v1/projects/${PROJECT_TWO}`
          : null
      })
  })));
  const client = clientFor(mock, {
    initialEtag: ETAG_ONE
  });
  const operation = {
    op: "addControlledUnit",
    clientRef: "fft",
    typeId: "pamguard.fft",
    name: null,
    dependencyPolicy: "add-defaults"
  };

  await client.mutate({ operations: [operation] });
  await client.newProject({
    name: "Fresh",
    discardDirty: true
  });
  await client.openProject({
    projectId: PROJECT_ONE
  });
  await client.save();
  await client.saveAs({ name: "Saved copy" });

  assert.equal(client.activeEtag, ETAG_SIX);
  assert.equal(mock.calls.length, 5);
  assert.equal(mock.steps.length, 0);
});

test("exposes a 412 conflict without adopting or retrying its winning ETag", async () => {
  const mock = mockFetch([{
    check(call) {
      assert.equal(
        call.options.headers["If-Match"],
        ETAG_ONE);
    },
    response: jsonResponse({
      error: "The active project changed since it was read",
      code: "precondition_failed",
      currentEtag: ETAG_TWO
    }, {
      status: 412,
      etag: ETAG_TWO
    })
  }]);
  const client = clientFor(mock, {
    initialEtag: ETAG_ONE
  });

  await assert.rejects(
    client.mutate({
      operations: [],
      validateOnly: true
    }),
    (error) => {
      assert.ok(error instanceof ProjectConflictError);
      assert.equal(error.status, 412);
      assert.equal(error.currentEtag, ETAG_TWO);
      assert.equal(error.code, "precondition_failed");
      return true;
    });
  assert.equal(mock.calls.length, 1);
  assert.equal(
    client.activeEtag,
    ETAG_ONE,
    "a conflict token was adopted without its active snapshot");
});

test("derived reads cannot advance the ETag beyond the loaded snapshot", async () => {
  const mock = mockFetch([
    {
      response: jsonResponse(
        activeSnapshot(ETAG_ONE),
        { etag: ETAG_ONE })
    },
    {
      response: jsonResponse({
        schemaVersion: 1,
        projectId: PROJECT_ONE,
        workingRevision: 1,
        authorityRevision: 1,
        projection: {}
      }, { etag: ETAG_TWO })
    }
  ]);
  const client = clientFor(mock);

  await client.loadActive();
  await assert.rejects(
    client.loadInspection(),
    (error) => {
      assert.ok(error instanceof ProjectConflictError);
      assert.equal(error.code, "active_project_changed");
      assert.equal(error.currentEtag, ETAG_TWO);
      return true;
    });
  assert.equal(
    client.activeEtag,
    ETAG_ONE,
    "inspection adopted an ETag without its active snapshot");
  assert.equal(mock.calls.length, 2);
  assert.equal(mock.steps.length, 0);
});

test("does not replace a valid ETag when response header and body disagree", async () => {
  const mock = mockFetch([{
    response: jsonResponse(
      activeSnapshot(ETAG_THREE),
      { etag: ETAG_TWO })
  }]);
  const client = clientFor(mock, {
    initialEtag: ETAG_ONE
  });

  await assert.rejects(
    client.loadActive(),
    (error) => {
      assert.ok(error instanceof ProjectProtocolError);
      assert.match(error.message, /ETags disagree/);
      return true;
    });
  assert.equal(client.activeEtag, ETAG_ONE);
});

test("captures If-Match when a command is issued, preventing queued blind overwrite", async () => {
  const seenEtags = [];
  const mock = mockFetch([
    {
      check(call) {
        seenEtags.push(
          call.options.headers["If-Match"]);
      },
      response: jsonResponse({
        schemaVersion: 1,
        changed: true,
        validatedOnly: false,
        createdEntities: [],
        active: activeSnapshot(ETAG_TWO)
      }, { etag: ETAG_TWO })
    },
    {
      check(call) {
        seenEtags.push(
          call.options.headers["If-Match"]);
      },
      response: jsonResponse({
        error: "stale",
        code: "precondition_failed",
        currentEtag: ETAG_TWO
      }, {
        status: 412,
        etag: ETAG_TWO
      })
    }
  ]);
  const client = clientFor(mock, {
    initialEtag: ETAG_ONE
  });

  const first = client.mutate({ operations: [] });
  const second = client.mutate({ operations: [] });
  await first;
  await assert.rejects(
    second,
    ProjectConflictError);

  assert.deepEqual(
    seenEtags,
    [ETAG_ONE, ETAG_ONE]);
  assert.equal(client.activeEtag, ETAG_TWO);
});

test("rejects non-strict command input before network access", async () => {
  const mock = mockFetch([]);
  const client = clientFor(mock);

  await assert.rejects(
    client.save(),
    ProjectStateError);
  await assert.rejects(
    client.newProject({
      name: "Wrong",
      discardDirty: false,
      expectedRevision: 1
    }),
    /unknown field 'expectedRevision'/);
  await assert.rejects(
    client.loadSavedProject("not-a-uuid"),
    /lowercase UUIDv4/);
  assert.equal(mock.calls.length, 0);
});

test("disposal and caller AbortSignal cancel in-flight requests", async () => {
  let firstStarted;
  const firstReady = new Promise((resolve) => {
    firstStarted = resolve;
  });
  const fetchImpl = async (_url, { signal }) =>
    new Promise((_resolve, reject) => {
      firstStarted();
      signal.addEventListener(
        "abort",
        () => reject(signal.reason),
        { once: true });
    });
  const client = createProjectClient({
    fetchImpl
  });
  const pending = client.loadCatalogue();
  await firstReady;
  client.dispose();
  await assert.rejects(
    pending,
    (error) => error?.name === "AbortError");
  assert.equal(client.disposed, true);
  assert.equal(client.activeEtag, null);
  await assert.rejects(
    client.loadActive(),
    (error) => error?.name === "AbortError");

  let secondStarted;
  const secondReady = new Promise((resolve) => {
    secondStarted = resolve;
  });
  const signalClient = createProjectClient({
    fetchImpl: async (_url, { signal }) =>
      new Promise((_resolve, reject) => {
        secondStarted();
        signal.addEventListener(
          "abort",
          () => reject(signal.reason),
          { once: true });
      })
  });
  const controller = new AbortController();
  const signalled = signalClient.loadCatalogue({
    signal: controller.signal
  });
  await secondReady;
  controller.abort();
  await assert.rejects(
    signalled,
    (error) => error?.name === "AbortError");
  signalClient.dispose();
});
