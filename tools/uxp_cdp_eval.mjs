#!/usr/bin/env node

import fs from "node:fs";

function usage(message) {
  if (message) console.error(message);
  console.error(
    "usage: uxp_cdp_eval.mjs --url ws://127.0.0.1:PORT/PATH " +
    "(--expression CODE | --expression-file PATH) [--timeout-ms N]"
  );
  process.exit(2);
}

const args = process.argv.slice(2);
let endpoint = "";
let expression = "";
let expressionFile = "";
let timeoutMs = 30000;
for (let index = 0; index < args.length; index++) {
  const flag = args[index];
  const value = args[index + 1];
  if (flag === "--url" && value) {
    endpoint = value;
    index++;
  } else if (flag === "--expression" && value !== undefined) {
    expression = value;
    index++;
  } else if (flag === "--expression-file" && value) {
    expressionFile = value;
    index++;
  } else if (flag === "--timeout-ms" && value) {
    timeoutMs = Number(value);
    index++;
  } else {
    usage(`argumento desconhecido ou incompleto: ${flag}`);
  }
}

if (!endpoint || Boolean(expression) === Boolean(expressionFile)) usage();
if (!Number.isInteger(timeoutMs) || timeoutMs < 1000 || timeoutMs > 300000)
  usage("--timeout-ms deve estar entre 1000 e 300000");

let url;
try {
  url = new URL(endpoint);
} catch {
  usage("URL WebSocket inválida");
}
if (url.protocol !== "ws:" ||
    !["127.0.0.1", "localhost", "[::1]"].includes(url.hostname)) {
  usage("somente endpoints WebSocket locais sem TLS são aceitos");
}

if (expressionFile) expression = fs.readFileSync(expressionFile, "utf8");
if (!expression.trim()) usage("a expressão está vazia");

const enableRequestId = 1;
const evaluateRequestId = 2;
const socket = new WebSocket(url);
let completed = false;
let runtimeEnabled = false;
let contextId = null;
let evaluationSent = false;
const timer = setTimeout(() => {
  if (completed) return;
  completed = true;
  console.error("tempo esgotado aguardando o console UXP");
  socket.close();
  process.exitCode = 1;
}, timeoutMs);

socket.addEventListener("open", () => {
  socket.send(JSON.stringify({
    id: enableRequestId,
    method: "Runtime.enable"
  }));
});

socket.addEventListener("message", event => {
  let payload;
  try {
    payload = JSON.parse(String(event.data));
  } catch {
    if (completed) return;
    completed = true;
    clearTimeout(timer);
    console.error("o console UXP retornou uma mensagem inválida");
    process.exitCode = 1;
    socket.close();
    return;
  }
  if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
    if (completed) return;
    completed = true;
    clearTimeout(timer);
    console.error("o console UXP retornou uma mensagem inválida");
    process.exitCode = 1;
    socket.close();
    return;
  }
  if (completed) return;
  if (payload.method === "Runtime.executionContextCreated") {
    const context = payload.params?.context;
    if (context && (context.auxData?.isDefault || contextId === null))
      contextId = context.id;
  } else if (payload.id === enableRequestId) {
    if (payload.error) {
      completed = true;
      clearTimeout(timer);
      console.log(JSON.stringify(payload, null, 2));
      process.exitCode = 1;
      socket.close();
      return;
    }
    runtimeEnabled = true;
  }
  if (runtimeEnabled && contextId !== null && !evaluationSent) {
    evaluationSent = true;
    socket.send(JSON.stringify({
      id: evaluateRequestId,
      method: "Runtime.evaluate",
      params: {
        expression,
        contextId,
        awaitPromise: true,
        returnByValue: true,
        userGesture: true,
        generatePreview: true
      }
    }));
  }
  if (payload.id !== evaluateRequestId) return;
  completed = true;
  clearTimeout(timer);
  console.log(JSON.stringify(payload, null, 2));
  if (payload.error || payload.result?.exceptionDetails) process.exitCode = 1;
  socket.close();
});

socket.addEventListener("error", () => {
  if (completed) return;
  completed = true;
  clearTimeout(timer);
  console.error("falha ao conectar ao console UXP local");
  process.exitCode = 1;
});

socket.addEventListener("close", () => {
  if (completed) return;
  completed = true;
  clearTimeout(timer);
  console.error("o console UXP encerrou a conexão antes da resposta");
  process.exitCode = 1;
});
