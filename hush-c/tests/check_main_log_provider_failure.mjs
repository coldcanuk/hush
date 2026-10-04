// Main-log bar: a missing-runtime reply is visible on the root card in
// #stream. Opening #thread-pane is not required. No model reply is invented.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import vm from "node:vm";

const here = dirname(fileURLToPath(import.meta.url));
const htmlPath = join(here, "..", "demo", "index.html");
const html = readFileSync(htmlPath, "utf8");
const start = html.indexOf("<script>");
const end = html.lastIndexOf("</script>");
if (start < 0 || end < start) {
  console.error("main-log failure check: demo script not found");
  process.exit(1);
}
const script = html.slice(start + "<script>".length, end);

const FAIL = "No selected provider is ready for Major. Open Configure Providers to check its harness login or API model and credentials.";
const ON_DECK = "At ease. I am on deck. Standing orders are noted. — Major";
const NORMAL = "A normal reply that must stay in the thread.";

function fail(msg) {
  console.error("main-log failure check: " + msg);
  process.exit(1);
}

class ClassList {
  constructor(el) { this.el = el; }
  parts() { return String(this.el.className || "").split(/\s+/).filter(Boolean); }
  write(parts) { this.el.className = parts.join(" "); }
  add(...xs) {
    const p = this.parts();
    xs.forEach((x) => { if (x && p.indexOf(x) < 0) p.push(x); });
    this.write(p);
  }
  remove(...xs) { this.write(this.parts().filter((x) => xs.indexOf(x) < 0)); }
  contains(x) { return this.parts().indexOf(x) >= 0; }
  toggle(x, force) {
    const on = force === undefined ? !this.contains(x) : !!force;
    if (on) this.add(x); else this.remove(x);
    return on;
  }
}

class El {
  constructor(tag) {
    this.nodeType = 1;
    this.tagName = String(tag || "div").toUpperCase();
    this.id = "";
    this.className = "";
    this.classList = new ClassList(this);
    this.children = [];
    this.parentElement = null;
    this.style = new Proxy({
      setProperty() {},
      getPropertyValue() { return ""; },
      removeProperty() {}
    }, { set(t, k, v) { t[k] = v; return true; } });
    this.dataset = {};
    this.attrs = {};
    this.hidden = false;
    this.value = "";
    this.checked = false;
    this.disabled = false;
    this.type = "";
    this.title = "";
    this.scrollTop = 0;
    this.scrollHeight = 40;
    this.clientHeight = 40;
    this._text = "";
    this.listeners = {};
  }
  get textContent() {
    if (this.children.length)
      return this.children.map((c) => c.textContent || "").join("");
    return this._text;
  }
  set textContent(v) {
    this._text = v == null ? "" : String(v);
    this.children = [];
  }
  get innerHTML() { return this._html == null ? this.textContent : this._html; }
  set innerHTML(v) {
    this._html = v == null ? "" : String(v);
    this._text = this._html.replace(/<[^>]*>/g, "");
    this.children = [];
  }
  appendChild(child) {
    if (!child) return child;
    if (child.parentElement && child.parentElement !== this) {
      child.parentElement.children = child.parentElement.children.filter((c) => c !== child);
    }
    child.parentElement = this;
    this.children.push(child);
    this._html = null;
    return child;
  }
  removeChild(child) {
    this.children = this.children.filter((c) => c !== child);
    if (child) child.parentElement = null;
    return child;
  }
  replaceChildren(...nodes) {
    this.children = [];
    this._text = "";
    this._html = null;
    nodes.flat().forEach((n) => { if (n) this.appendChild(n); });
  }
  get firstChild() { return this.children[0] || null; }
  insertBefore(node, before) {
    if (!node) return node;
    const i = before ? this.children.indexOf(before) : -1;
    if (node.parentElement && node.parentElement !== this)
      node.parentElement.children = node.parentElement.children.filter((c) => c !== node);
    node.parentElement = this;
    if (i < 0) this.children.push(node);
    else this.children.splice(i, 0, node);
    return node;
  }
  contains(node) {
    if (node === this) return true;
    return this.children.some((c) => c.contains && c.contains(node));
  }
  closest() { return null; }
  querySelector() { return null; }
  querySelectorAll() { return []; }
  addEventListener(type, fn) { (this.listeners[type] || (this.listeners[type] = [])).push(fn); }
  removeEventListener() {}
  setAttribute(k, v) { this.attrs[k] = String(v); }
  getAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attrs, k) ? this.attrs[k] : null; }
  removeAttribute(k) { delete this.attrs[k]; }
  hasAttribute(k) { return Object.prototype.hasOwnProperty.call(this.attrs, k); }
  focus() {}
  click() {}
  remove() {
    if (this.parentElement) this.parentElement.removeChild(this);
  }
  getBoundingClientRect() {
    return { width: 400, height: 300, top: 0, left: 0, right: 400, bottom: 300 };
  }
  setPointerCapture() {}
  releasePointerCapture() {}
}

class TextNode {
  constructor(text) {
    this.nodeType = 3;
    this.textContent = text == null ? "" : String(text);
    this.parentElement = null;
    this.children = [];
  }
  contains() { return false; }
}

const byId = {};
function element(id) {
  if (!byId[id]) {
    const el = new El("div");
    el.id = id;
    byId[id] = el;
  }
  return byId[id];
}

const document = {
  getElementById: (id) => element(id),
  createElement: (tag) => new El(tag),
  createTextNode: (t) => new TextNode(t),
  createDocumentFragment() {
    const el = new El("fragment");
    return el;
  },
  querySelector() { return null; },
  querySelectorAll() { return []; },
  addEventListener() {},
  documentElement: new El("html"),
  body: new El("body"),
  head: new El("head"),
  title: ""
};
document.documentElement.style = {};

const storage = {};
const localStorage = {
  getItem: (k) => (Object.prototype.hasOwnProperty.call(storage, k) ? storage[k] : null),
  setItem: (k, v) => { storage[k] = String(v); },
  removeItem: (k) => { delete storage[k]; }
};

const sandbox = {
  console,
  document,
  localStorage,
  location: { protocol: "http:", href: "http://127.0.0.1/", host: "127.0.0.1" },
  navigator: {
    serviceWorker: { register: () => Promise.resolve() },
    clipboard: { writeText: async () => {} }
  },
  window: null,
  self: null,
  globalThis: null,
  setTimeout: () => 0,
  clearTimeout() {},
  setInterval: () => 0,
  clearInterval() {},
  fetch: () => Promise.reject(new Error("no relay")),
  matchMedia: () => ({ matches: false, addEventListener() {}, addListener() {} }),
  getComputedStyle: () => ({ getPropertyValue: () => "" }),
  CSS: { escape: (s) => String(s) },
  requestAnimationFrame: (fn) => { if (fn) fn(); return 0; },
  cancelAnimationFrame() {},
  alert() {},
  confirm: () => false,
  URL,
  URLSearchParams,
  TextEncoder,
  TextDecoder,
  crypto: { getRandomValues: (a) => a, subtle: {} },
  performance: { now: () => Date.now() },
  MutationObserver: class { observe() {} disconnect() {} },
  ResizeObserver: class { observe() {} disconnect() {} },
  IntersectionObserver: class { observe() {} disconnect() {} },
  WebSocket: class { close() {} send() {} },
  Event: class {},
  CustomEvent: class {},
  KeyboardEvent: class {},
  PointerEvent: class {}
};
sandbox.window = sandbox;
sandbox.self = sandbox;
sandbox.globalThis = sandbox;
sandbox.window.matchMedia = sandbox.matchMedia;
sandbox.window.getComputedStyle = sandbox.getComputedStyle;
sandbox.window.addEventListener = () => {};
sandbox.window.removeEventListener = () => {};

let bootError = null;
const context = vm.createContext(sandbox);
try {
  vm.runInContext(script, context, { filename: "index.html" });
} catch (err) {
  bootError = err;
}
if (bootError) {
  console.error(bootError.stack || bootError);
  fail("demo script did not finish booting");
}

const render = sandbox.render;
if (typeof render !== "function") {
  fail("render is not available after loading demo/index.html");
}

const root = "a".repeat(64);
const other = "d".repeat(64);
const now = Math.floor(Date.now() / 1000);
const MAJOR_NPUB = "npub1majormajormajormajormajormajormajormajormajormajormajor";
if (typeof sandbox.applySession !== "function")
  fail("applySession is not available");
sandbox.applySession({
  logged_in: false,
  ready: false,
  channels: [],
  payne: { name: "Major", npub: MAJOR_NPUB, pubkey: "majorpub" }
});
const events = [
  {
    id: root, kind: 1, channel: "welcome", reply_to: "",
    content: "@Major what time is it", pubkey: "humanpub", created_at: now,
    mentions: [MAJOR_NPUB]
  },
  {
    id: "b".repeat(64), kind: 1, channel: "welcome", reply_to: root,
    content: ON_DECK, pubkey: "majorpub", created_at: now
  },
  {
    id: "c".repeat(64), kind: 1, channel: "welcome", reply_to: root,
    content: FAIL, pubkey: "majorpub", created_at: now
  },
  {
    id: other, kind: 1, channel: "welcome", reply_to: "",
    content: "hello", pubkey: "humanpub", created_at: now,
    mentions: [MAJOR_NPUB]
  },
  {
    id: "e".repeat(64), kind: 1, channel: "welcome", reply_to: other,
    content: NORMAL, pubkey: "majorpub", created_at: now
  }
];

try {
  render(events, {
    ok: true, version: "0.0.1", events: 5, clients: 1,
    thinking: [
      { name: "Major", parent: root },
      { name: "Major", parent: other }
    ]
  });
} catch (err) {
  console.error(err && err.stack ? err.stack : err);
  fail("render threw");
}

const stream = document.getElementById("stream");
const pane = document.getElementById("thread-pane");
const streamText = stream.textContent || "";
const paneText = pane.textContent || "";

function walk(el, hit) {
  if (!el || el.nodeType === 3) return;
  if (el.classList && el.classList.contains("log-failure")) hit.push(el);
  (el.children || []).forEach((c) => walk(c, hit));
}
const lines = [];
walk(stream, lines);

if (streamText.indexOf(FAIL) < 0)
  fail("#stream does not include the failure sentence");
if (lines.length !== 1)
  fail("expected one .log-failure on the main log, got " + lines.length);
if (lines[0].textContent !== FAIL)
  fail("main-log failure text is not the relay sentence");
if (lines[0].getAttribute("role") !== "status")
  fail("main-log failure is not a status line");
if (pane.classList.contains("show"))
  fail("render opened #thread-pane");
if (paneText.indexOf(FAIL) >= 0)
  fail("failure was painted in the thread pane instead of only the main log requirement");
if (streamText.indexOf(ON_DECK) >= 0)
  fail("main log invented the on-deck line as a reply");
if (streamText.indexOf(NORMAL) >= 0)
  fail("main log copied a normal thread reply onto the card");
if (html.indexOf('class="log-failure"') < 0 && html.indexOf("line.className = \"log-failure\"") < 0)
  fail("demo source lost the main-log failure class");
if (/\.log-failure\s*\{[^}]*display\s*:\s*none/.test(html))
  fail(".log-failure is hidden");

function cardsOf(el) {
  return (el.children || []).filter((c) => c.classList && c.classList.contains("note"));
}
function progressAcks(el, out) {
  if (!el || el.nodeType === 3) return;
  if (el.classList && el.classList.contains("robot-ack")) out.push(el);
  (el.children || []).forEach((c) => progressAcks(c, out));
}
function inProgressText(s) {
  const t = String(s || "");
  return t.indexOf("is thinking") >= 0 || t.indexOf("is reacting") >= 0;
}
function ackSaysInProgress(el) {
  const cls = String(el.className || "");
  const title = String(el.title || "");
  return /\bthinking\b/.test(cls) || /\breacting\b/.test(cls) ||
    /\bthinking\b/.test(title) || /\breacting\b/.test(title);
}
function cardShowsInProgress(el) {
  if (!el) return false;
  if (inProgressText(el.textContent)) return true;
  if (el.classList && el.classList.contains("think")) return true;
  const acks = [];
  progressAcks(el, acks);
  return acks.some((a) => ackSaysInProgress(a) || inProgressText(a.textContent));
}
function freshCards() {
  const stream = document.getElementById("stream");
  const cards = cardsOf(stream);
  return {
    stream,
    failed: cards.find((c) => (c.textContent || "").indexOf(FAIL) >= 0),
    normal: cards.find((c) => (c.textContent || "").indexOf("hello") >= 0)
  };
}
let cards = freshCards();
const failedCard = cards.failed;
const normalCard = cards.normal;
if (!failedCard)
  fail("failure sentence is not on a root card");
if (cardShowsInProgress(failedCard))
  fail("root card shows a thinking or reacting mark while the failure sentence is on it");
if (!normalCard)
  fail("normal root card is missing");
if ((normalCard.textContent || "").indexOf(FAIL) >= 0)
  fail("normal card picked up the failure sentence");
if ((normalCard.textContent || "").indexOf("is thinking") < 0)
  fail("a normal card with no failure sentence lost its thinking mark");

/* A live job past the thinking window paints "reacting". The failure
 * card must not. A card with no failure sentence still may. */
vm.runInContext(`
  const realNow = Date.now.bind(Date);
  Date.now = () => realNow() + 1500;
`, context);
try {
  render(events, {
    ok: true, version: "0.0.1", events: 5, clients: 1,
    thinking: [
      { name: "Major", parent: root },
      { name: "Major", parent: other }
    ]
  });
} catch (err) {
  console.error(err && err.stack ? err.stack : err);
  fail("second render threw");
}
cards = freshCards();
if (!cards.failed)
  fail("failure sentence left the root card after the reacting window");
if (cardShowsInProgress(cards.failed))
  fail("root card shows thinking or reacting after the reacting window");
const failedAcks = [];
progressAcks(cards.failed, failedAcks);
if (failedAcks.some((a) => ackSaysInProgress(a)))
  fail("failure card robot-ack class or title still says thinking or reacting");
if ((cards.failed.textContent || "").indexOf("\u{1F44D}") < 0 &&
    (cards.failed.innerHTML || "").indexOf("\u{1F44D}") < 0) {
  const thumb = failedAcks.some((a) => String(a.innerHTML || a.textContent || "").indexOf("\u{1F44D}") >= 0);
  if (!thumb)
    fail("emoji thumb was removed from the failure card");
}
if (!cards.normal)
  fail("normal root card disappeared after the reacting window");
if ((cards.normal.textContent || "").indexOf("is reacting") < 0)
  fail("a normal card with no failure sentence did not reach reacting");
if ((cards.normal.textContent || "").indexOf(FAIL) >= 0)
  fail("normal card picked up the failure sentence after the reacting window");

console.log("main-log failure: #stream shows the provider sentence without the thread pane");
console.log("main-log failure: thinking and reacting marks are absent on the failure card");
console.log("main-log failure: a card with no failure sentence still shows thinking, then reacting");
