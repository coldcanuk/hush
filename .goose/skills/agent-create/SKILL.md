# Agent create (Sgt Major Payne)

Create a Hush agent (a robot on this vibe) via the relay HTTP API.
Use this when a human asks Payne to raise a robot, or when Goose
must create an agent with skills on the fly.

## Contract

1. Collect:
   - `name` (required)
   - `system_prompt` (standing orders)
   - optional context file: **plaintext or Markdown only**
2. Never put an nsec in chat.
3. POST JSON to the running relay (default `http://127.0.0.1:10555`).
4. Confirm from the session `agents[]` entry (`name`, `slug`, `npub`).
5. Tell the human the retrieve path:
   `pass show hush/agents/<slug>/nsec`

## MIME

Accepted context: `text/plain`, `text/markdown`, `text/x-markdown`,
or filename ending `.txt` / `.md` / `.markdown`.

Rejected: PDF, images, HTML, empty MIME with any other extension.
The server re-checks. Do not bypass the check.

## Request

```
POST /api/agent
Content-Type: application/json

{
  "name": "Sentry",
  "system_prompt": "Watch the perimeter.",
  "save_pass": true,
  "context_name": "brief.md",
  "context_mime": "text/markdown",
  "context_text": "# stand to"
}
```

`save_pass` defaults on. The human must opt out.

## Verify

```
curl -s http://127.0.0.1:10555/api/session | grep sentry
```

Session lists `{name, slug, npub, ncontext}`. nsec is never returned
after create.

## Voice

Payne: “State the robot’s name.” “Write its standing orders.”
“Attach only plain text or Markdown. I will refuse the rest.”
“Carry on.”
