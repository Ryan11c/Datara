import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const rootDir = resolve(__dirname, "..");
const dataFile = resolve(rootDir, "data", "logs.json");
const enginePath = resolve(rootDir, "cpp", process.platform === "win32" ? "log_engine.exe" : "log_engine");
const port = Number(process.env.PORT ?? 3000);
const openAiApiKey = process.env.OPENAI_API_KEY ?? "";
const openAiModel = process.env.OPENAI_MODEL ?? "gpt-5.4-mini";

type JsonBody = Record<string, unknown>;
type LogPayload = Record<string, unknown>;
type AskPlan = {
  query: string;
  aggregate: string;
  page: number;
  limit: number;
  explanation: string;
};

function sendJson(response: ServerResponse, status: number, body: JsonBody) {
  response.writeHead(status, { "content-type": "application/json" });
  response.end(JSON.stringify(body));
}

function readBody(request: IncomingMessage): Promise<string> {
  return new Promise((resolveBody, reject) => {
    let body = "";
    request.on("data", (chunk) => {
      body += chunk;
    });
    request.on("end", () => resolveBody(body));
    request.on("error", reject);
  });
}

function readLogsFromDisk(): LogPayload[] {
  if (!existsSync(dataFile)) {
    return [];
  }

  const raw = readFileSync(dataFile, "utf8").trim();
  if (!raw) {
    return [];
  }

  const parsed = JSON.parse(raw);
  return Array.isArray(parsed) ? parsed : [];
}

function writeLogsToDisk(logs: LogPayload[]) {
  mkdirSync(dirname(dataFile), { recursive: true });
  writeFileSync(dataFile, JSON.stringify(logs, null, 2));
}

function appendLogsToDisk(logs: LogPayload[]) {
  writeLogsToDisk(readLogsFromDisk().concat(logs));
}

function parseLogsPayload(parsed: unknown): LogPayload[] | null {
  const logs = Array.isArray(parsed) ? parsed : (parsed as any)?.logs;
  return Array.isArray(logs) ? logs : null;
}

function extractResponseText(responseBody: any): string {
  const output = Array.isArray(responseBody?.output) ? responseBody.output : [];
  const textParts: string[] = [];

  for (const item of output) {
    const content = Array.isArray(item?.content) ? item.content : [];
    for (const part of content) {
      if (part?.type === "output_text" && typeof part.text === "string") {
        textParts.push(part.text);
      }
    }
  }

  return textParts.join("");
}

async function translateQuestionToPlan(question: string): Promise<AskPlan> {
  if (!openAiApiKey) {
    throw new Error("Missing OPENAI_API_KEY");
  }

  const schema = {
    type: "object",
    additionalProperties: false,
    required: ["query", "aggregate", "page", "limit", "explanation"],
    properties: {
      query: { type: "string" },
      aggregate: { type: "string" },
      page: { type: "integer", minimum: 1 },
      limit: { type: "integer", minimum: 1, maximum: 500 },
      explanation: { type: "string" },
    },
  };

  const prompt = [
    "Translate the user's request into the log engine query language used by this backend.",
    "Supported fields: service, level, status, latency, ts.",
    "Supported operators: =, >, <.",
    "Only join conditions with AND.",
    "If the user asks for counts or grouped summaries, use aggregate values: '', 'count', 'count_by_service', or 'avg_latency_by_service'.",
    "If the user does not mention pagination, use page=1 and limit=50.",
    "Return only valid JSON matching the schema.",
    "",
    `User question: ${question}`,
  ].join("\n");

  const response = await fetch("https://api.openai.com/v1/responses", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${openAiApiKey}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      model: openAiModel,
      input: [
        {
          role: "developer",
          content: [
            {
              type: "input_text",
              text: "You translate natural language into deterministic log-engine queries. Never invent unsupported syntax.",
            },
          ],
        },
        {
          role: "user",
          content: [
            {
              type: "input_text",
              text: prompt,
            },
          ],
        },
      ],
      text: {
        format: {
          type: "json_schema",
          name: "log_query_translation",
          strict: true,
          schema,
        },
      },
    }),
  });

  if (!response.ok) {
    throw new Error(`OpenAI request failed with status ${response.status}`);
  }

  const responseBody = await response.json();
  const text = extractResponseText(responseBody);
  if (!text.trim()) {
    throw new Error("OpenAI response did not contain text output");
  }

  const parsed = JSON.parse(text);
  return {
    query: String(parsed.query ?? "").trim(),
    aggregate: String(parsed.aggregate ?? ""),
    page: Number(parsed.page ?? 1),
    limit: Number(parsed.limit ?? 50),
    explanation: String(parsed.explanation ?? ""),
  };
}

async function runEngine(query: string, mode = "indexed", page = 1, limit = 100, aggregate = ""): Promise<JsonBody> {
  return new Promise((resolveRun, reject) => {
    const args = [
      "--file",
      dataFile,
      "--query",
      query,
      "--mode",
      mode,
      "--page",
      String(page),
      "--limit",
      String(limit),
    ];

    if (aggregate) {
      args.push("--aggregate", aggregate);
    }

    const child = spawn(enginePath, args, {
      cwd: rootDir,
    });

    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code !== 0) {
        reject(new Error(stderr || `Engine exited with code ${code}`));
        return;
      }

      resolveRun(JSON.parse(stdout));
    });
  });
}

const server = createServer(async (request, response) => {
  try {
    const url = new URL(request.url ?? "/", `http://${request.headers.host ?? "localhost"}`);

    if (request.method === "GET" && url.pathname === "/health") {
      sendJson(response, 200, { ok: true, engineBuilt: existsSync(enginePath) });
      return;
    }

    if (request.method === "POST" && url.pathname === "/ingest") {
      const raw = await readBody(request);
      const parsed = JSON.parse(raw);
      const logs = parseLogsPayload(parsed);

      if (!logs) {
        sendJson(response, 400, { error: "Expected a JSON array or { logs: [...] }" });
        return;
      }

      if (parsed?.replace === true) {
        writeLogsToDisk(logs);
      } else {
        appendLogsToDisk(logs);
      }

      sendJson(response, 200, { ingested: logs.length, replace: parsed?.replace === true });
      return;
    }

    if (request.method === "POST" && url.pathname === "/query") {
      const raw = await readBody(request);
      const body = JSON.parse(raw);
      const query = String(body.query ?? "");
      const mode = String(body.mode ?? "indexed");
      const page = Number(body.page ?? url.searchParams.get("page") ?? 1);
      const limit = Number(body.limit ?? url.searchParams.get("limit") ?? 100);
      const aggregate = String(body.aggregate ?? "");

      if (!query.trim()) {
        sendJson(response, 400, { error: "Missing query" });
        return;
      }

      sendJson(response, 200, await runEngine(query, mode, page, limit, aggregate));
      return;
    }

    if (request.method === "POST" && url.pathname === "/ask") {
      const raw = await readBody(request);
      const body = JSON.parse(raw);
      const question = String(body.question ?? "");
      const mode = String(body.mode ?? "indexed");
      const execute = body.execute !== false;

      if (!question.trim()) {
        sendJson(response, 400, { error: "Missing question" });
        return;
      }

      const plan = await translateQuestionToPlan(question);
      if (!plan.query) {
        sendJson(response, 400, { error: "Model could not produce a query" });
        return;
      }

      if (!execute) {
        sendJson(response, 200, {
          question,
          query: plan.query,
          aggregate: plan.aggregate,
          page: plan.page,
          limit: plan.limit,
          explanation: plan.explanation,
          executed: false,
        });
        return;
      }

      sendJson(response, 200, {
        question,
        query: plan.query,
        aggregate: plan.aggregate,
        page: plan.page,
        limit: plan.limit,
        explanation: plan.explanation,
        executed: true,
        result: await runEngine(plan.query, mode, plan.page, plan.limit, plan.aggregate),
      });
      return;
    }

    sendJson(response, 404, { error: "Not found" });
  } catch (error) {
    sendJson(response, 500, { error: error instanceof Error ? error.message : "Unknown error" });
  }
});

server.listen(port, () => {
  console.log(`Log API listening on http://localhost:${port}`);
});
