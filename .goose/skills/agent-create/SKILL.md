# Agent create (Sgt Major Payne)

Create a Hush agent (a robot on this vibe) via the relay HTTP API.
Use this when a human asks Payne to raise a robot, or when Goose
must create an agent with skills on the fly.

## Contract

1. Collect:
   - `name` (optional; empty becomes `Robot-XXXX`)
   - `system_prompt` (**required**)
   - `provider` (**required**) — one of:
     `goose`, `grok-build`, `codex`, `cline`,
     `gemini-api`, `xai-api`, `openai-api`, `anthropic-api`
   - optional: tell the human to click the pencil and configure that
     provider (home config, or API key + host + model). Raise does
     not require configure to succeed.
   - optional context files: **plaintext or Markdown only**, max 3
2. Never put an nsec in chat.
3. POST JSON to the running relay (default `http://127.0.0.1:10555`).
4. Confirm from the session `agents[]` entry (`name`, `slug`, `npub`, `provider`).
5. Tell the human the retrieve path:
   `pass show hush/agents/<slug>/nsec`

## MIME

Accepted context: `text/plain`, `text/markdown`, `text/x-markdown`,
or filename ending `.txt` / `.md` / `.markdown`.

Rejected: PDF, images, HTML, empty MIME with any other extension.
The server re-checks. Do not bypass the check. Max 3 files.

## Request

```
POST /api/agent
Content-Type: application/json

{
  "name": "Sentry",
  "system_prompt": "Watch the perimeter.",
  "provider": "goose",
  "save_pass": true,
  "context_name_0": "brief.md",
  "context_mime_0": "text/markdown",
  "context_text_0": "# stand to"
}
```

`save_pass` defaults on. The human must opt out.

## Delete

```
POST /api/agent
{ "action": "delete", "slug": "sentry" }
```

Payne (`sgt-major-payne`) cannot be deleted.

## Verify

```
curl -s http://127.0.0.1:10555/api/session | grep sentry
```

Session lists `{name, slug, npub, provider, prompt, ncontext}`. nsec is
never returned after create.

## Voice

Payne: “State the robot’s name.” “Write its system prompt.”
“Choose an AI provider.” “Configure this provider.”
“Attach only plain text or Markdown. I will refuse the rest.”
“Carry on.”

## Configure (optional)

```
GET  /api/provider
POST /api/provider {provider, use_home?, host?, model?, api_key?}
POST /api/provider/scan {provider, host?, api_key?}
```

Keys live at `pass show hush/providers/<id>/api_key`. Never echo them.
