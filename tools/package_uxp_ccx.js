#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

async function main() {
  const [manifestPath, packageDir] = process.argv.slice(2);
  if (!manifestPath || !packageDir)
    throw new Error("usage: package_uxp_ccx.js MANIFEST PACKAGE_DIR");

  const udtResources =
    "/Applications/Adobe UXP Developer Tools/Adobe UXP Developer Tools.app" +
    "/Contents/Resources/app.asar";
  const commandPath = path.join(
    udtResources,
    "node_modules/@adobe/uxp-devtools-core/src/core/client/plugin/actions/" +
      "PluginPackageCommand.js"
  );
  const PluginPackageCommand = require(commandPath);
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  const params = {
    manifest: manifestPath,
    packageDir,
    apps: ["premierepro"]
  };
  const command = new PluginPackageCommand(null, params);
  const manifests = command.getManifestsForPackaging(
    manifest, params.apps);
  if (manifests.length !== 1 || manifests[0].host.app !== "premierepro")
    throw new Error("manifest does not resolve to exactly one Premiere host");
  command.validateForPackaging(manifests[0], manifests[0].host);
  const results = await command.package();
  if (results.length !== 1 || !results[0].success ||
      results[0].host !== "premierepro")
    throw results[0] && results[0].error ? results[0].error :
      new Error("UDT packaging failed");

  const expected = path.join(
    packageDir, `${manifest.id}_premierepro.ccx`);
  if (!fs.existsSync(expected))
    throw new Error(`UDT did not create ${expected}`);
  process.stdout.write(expected + "\n");
}

main().catch(error => {
  process.stderr.write(String(error && error.stack ? error.stack : error) +
    "\n");
  process.exitCode = 1;
});
