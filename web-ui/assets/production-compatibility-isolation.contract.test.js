"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const webRoot = path.resolve(__dirname, "..");
const repositoryRoot = path.resolve(webRoot, "..");
const indexPath = path.join(webRoot, "index.html");
const legacyPath = path.join(webRoot, "legacy-compat.html");
const index = fs.readFileSync(indexPath, "utf8");
const legacy = fs.readFileSync(legacyPath, "utf8");

const productionScriptUrls = Array.from(
  index.matchAll(/<script[^>]+src="([^"]+)"/g),
  (match) => match[1]);
assert(productionScriptUrls.length > 0, "production index has no scripts");
assert(
  productionScriptUrls.every((url) => url.startsWith("/assets/")),
  "production index loads a script outside the confined asset root");
assert(
  productionScriptUrls.every((url) =>
    !/(?:^|\/)(?:legacy-compat|data-model|displays|diagnostics|settings|shell|main)\.js$/
      .test(url)),
  "production index imports a legacy application asset");

const productionSources = [
  index,
  ...productionScriptUrls.map((url) =>
    fs.readFileSync(
      path.join(webRoot, url.replace(/^\/+/, "")),
      "utf8"))
].join("\n");

const forbiddenProductionPatterns = new Map([
  ["AnalysisSession symbol", /\bAnalysisSession\b/],
  ["legacy session route", /["'`]\/sessions(?:[/?#"'`]|$)/],
  ["legacy workspace route", /["'`]\/workspaces(?:[/?#"'`]|$)/],
  ["low-level graph route", /["'`]\/module-graph(?:[/?#"'`]|$)/],
  ["low-level acquisition ingress", /["'`]\/module-runtime\/acquisitions(?:[/?#"'`]|$)/],
  ["low-level operator-input ingress", /["'`]\/module-runtime\/operator-inputs(?:[/?#"'`]|$)/],
  ["legacy capture lifecycle", /["'`]\/capture\/(?:status|start|stop)(?:[/?#"'`]|$)/],
  ["legacy entry point", /legacy-compat(?:ibility)?\.html|legacy-compat\.js/],
  ["browser graph-layout authority", /\bGRAPH_LAYOUT_STORAGE_KEY\b/],
  ["browser workspace authority", /\bworkspaceStorageKey\b/]
]);
for (const [label, pattern] of forbiddenProductionPatterns) {
  assert(!pattern.test(productionSources), `production bundle contains ${label}`);
}

for (const legacyDomId of [
  "sessionId",
  "tab-workspace",
  "tab-spectrogram",
  "tab-clicks",
  "tab-detections",
  "tab-archive",
  "tab-console",
  "workspaceAudioSource",
  "workspaceArrangement"
]) {
  assert(
    !index.includes(`id="${legacyDomId}"`),
    `production index contains hidden legacy DOM #${legacyDomId}`);
}
assert.equal(
  (index.match(/data-pamguard-tab-kind="data-model"/g) || []).length,
  1,
  "production index must start with exactly one Data Model tab");

assert.match(
  legacy,
  /<title>PAMGuard Legacy Compatibility \/ Oracle Harness<\/title>/);
assert.match(
  legacy,
  /data-pamguard-entrypoint="legacy-compatibility-oracle"/);
assert.match(
  legacy,
  /data-compatibility-harness="analysis-session-oracle"/);
assert.match(legacy, /src="\/assets\/legacy-compat\.js"/);

const dockerfile = fs.readFileSync(
  path.join(repositoryRoot, "Dockerfile.engine"),
  "utf8");
const dockerignore = fs.readFileSync(
  path.join(repositoryRoot, ".dockerignore"),
  "utf8");
const serviceSource = fs.readFileSync(
  path.join(
    repositoryRoot,
    "cpp-engine",
    "tools",
    "pamguard_engine_service.cpp"),
  "utf8");
assert(
  !/COPY\s+web-ui\s+\/app\/web-ui/.test(dockerfile),
  "production image broadly copies the whole web-ui tree");

const dockerignoreRules = dockerignore
  .split(/\r?\n/)
  .map((line) => line.trim())
  .filter((line) => line && !line.startsWith("#"))
  .map((line) => ({
    include: line.startsWith("!"),
    pattern: (line.startsWith("!") ? line.slice(1) : line)
      .replace(/\\/g, "/")
      .replace(/^\/+/, "")
  }));
function dockerignorePatternMatches(pattern, relativePath, isDirectory) {
  const directoryPattern = pattern.endsWith("/");
  if (directoryPattern && !isDirectory) {
    return false;
  }
  const normalizedPattern = pattern.replace(/\/+$/, "");
  if (normalizedPattern === "*") {
    return true;
  }
  const escaped = normalizedPattern.replace(
    /[.+^${}()|[\]\\]/g,
    "\\$&");
  const globbed = escaped
    .replace(/\*\*/g, "\u0000")
    .replace(/\*/g, "[^/]*")
    .replace(/\?/g, "[^/]")
    .replace(/\u0000/g, ".*");
  const prefix = normalizedPattern.includes("/") ? "^" : "(?:^|/)";
  return new RegExp(`${prefix}${globbed}$`)
    .test(relativePath);
}
function sourceSurvivesDockerignore(relativePath) {
  const segments = relativePath.split("/");
  return segments.every((_, index) => {
    const candidate = segments.slice(0, index + 1).join("/");
    const isDirectory = index < segments.length - 1 ||
      fs.statSync(path.join(repositoryRoot, candidate)).isDirectory();
    let included = true;
    for (const rule of dockerignoreRules) {
      if (dockerignorePatternMatches(
        rule.pattern,
        candidate,
        isDirectory)) {
        included = rule.include;
      }
    }
    return included;
  });
}

const dockerfileLogicalLines = dockerfile
  .replace(/\\\r?\n/g, " ")
  .split(/\r?\n/)
  .map((line) => line.trim())
  .filter(Boolean);
const localCopySources = dockerfileLogicalLines.flatMap((line) => {
  if (!/^COPY\s+/i.test(line) || /^COPY\s+--from=/i.test(line)) {
    return [];
  }
  const fields = line.replace(/^COPY\s+/i, "").trim().split(/\s+/);
  assert(
    fields.length >= 2,
    `could not characterize Dockerfile COPY instruction: ${line}`);
  return fields.slice(0, -1);
});
assert(localCopySources.length > 0, "Dockerfile has no local COPY sources");
for (const source of localCopySources) {
  assert(
    !/[*?[\]]/.test(source),
    `Dockerfile COPY glob needs explicit contract support: ${source}`);
  const normalizedSource = source
    .replace(/\\/g, "/")
    .replace(/^\.\/+/, "")
    .replace(/\/+$/, "");
  assert(
    fs.existsSync(path.join(repositoryRoot, normalizedSource)),
    `Dockerfile COPY source does not exist: ${source}`);
  assert(
    sourceSurvivesDockerignore(normalizedSource),
    `Dockerfile COPY source is excluded by .dockerignore: ${source}`);
}
const productionAssetUrls = new Set([
  ...Array.from(
    index.matchAll(/(?:src|href)="(\/assets\/[^"]+)"/g),
    (match) => match[1]),
  ...Array.from(
    productionSources.matchAll(
      /["'](\/assets\/[^"']+\.(?:css|js|json))["']/g),
    (match) => match[1])
]);
for (const assetUrl of productionAssetUrls) {
  assert(
    dockerfile.includes(`web-ui${assetUrl}`),
    `production image omits required project asset ${assetUrl}`);
}
for (const legacyArtifact of [
  "legacy-compat.html",
  "legacy-compat.js",
  "ingest-sources.legacy-session-compat.example.json",
  "station-session.example.json",
  "array-session.example.json"
]) {
  assert(
    !dockerfile.includes(legacyArtifact),
    `production image copies compatibility artifact ${legacyArtifact}`);
}
for (const excludedAsset of [
  "web-ui/legacy-compat.html",
  "web-ui/assets/legacy-compat.js",
  "web-ui/assets/data-model.js",
  "web-ui/assets/displays.js"
]) {
  assert(
    dockerignore.includes(excludedAsset),
    `Docker context does not explicitly exclude ${excludedAsset}`);
}

const serviceLines = serviceSource.split(/\r?\n/);
const compatibilityRouteLines = serviceLines
  .map((line, index) => ({ line, index }))
  .filter(({ line }) =>
    /\bserver\.(?:Get|Post|Delete)\(/.test(line) &&
    (
      /\/sessions(?:[/"(]|$)/.test(line) ||
      /\/jobs(?:[/"(]|$)/.test(line) ||
      (
        /\bserver\.Get\(/.test(line) &&
        /\/workspaces(?:[/"(]|$)/.test(line)
      ) ||
      /\/capture\/(?:status|start|stop)/.test(line) ||
      /\/module-runtime\/(?:acquisitions|operator-inputs)/.test(line)
    ));
assert(
  compatibilityRouteLines.length >= 24,
  "service compatibility-route characterization is incomplete");
for (const { line, index } of compatibilityRouteLines) {
  const handlerPrologue = serviceLines
    .slice(index, index + 14)
    .join("\n");
  assert.match(
    handlerPrologue,
    /require_legacy_analysis_compatibility\(res\)/,
    `normal project mode does not gate ${line.trim()}`);
}

const moduleRuntimePostLines = serviceLines
  .map((line, index) => ({ line, index }))
  .filter(({ line }) =>
    /\bserver\.Post\(/.test(line) &&
    /\/module-runtime\//.test(line));
assert.equal(
  moduleRuntimePostLines.length,
  3,
  "module-runtime POST surface changed without an authority review");
for (const { line, index } of moduleRuntimePostLines) {
  const handlerPrologue = serviceLines
    .slice(index, index + 14)
    .join("\n");
  if (/\/module-runtime\/control/.test(line)) {
    assert.doesNotMatch(
      handlerPrologue,
      /require_legacy_analysis_compatibility\(res\)/,
      "project runtime control was incorrectly made compatibility-only");
    continue;
  }
  assert.match(
    handlerPrologue,
    /require_legacy_analysis_compatibility\(res\)/,
    `generated-ID runtime write is not compatibility-gated: ${line.trim()}`);
}
assert.match(
  serviceSource,
  /if \(legacy_model_compat &&\s*!session_config_dir\.empty\(\)/,
  "normal project mode can load persisted AnalysisSession state");
assert.match(
  serviceSource,
  /if \(legacy_model_compat && !job_audio_dir\.empty\(\)\)/,
  "normal project mode can start legacy offline-job workers");

console.log(
  "Production compatibility isolation contract passed: one project entry " +
  "point, no legacy route/DOM imports, gated compatibility APIs, explicit " +
  "local oracle harness, and no compatibility application in the runtime " +
  "image.");
