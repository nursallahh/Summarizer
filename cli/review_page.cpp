#include "review_page.hpp"

#include "json_writer.hpp"

namespace cli {

namespace {

std::string json_str(const std::string& s) {
    std::string out;
    smz::append_json_string(out, s);
    return out;
}

std::string json_array(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        out += (i ? "," : "") + json_str(values[i]);
    }
    return out + "]";
}

// The page is fixed; only the data block changes.
constexpr const char* kPageTop = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Summarizer review</title>
<style>
  :root { --bg:#f6f5f2; --card:#fff; --ink:#1d1d1f; --muted:#6b6b70; --line:#e2e0db;
          --ok:#1f8a4c; --changed:#c25a00; --low:#b3261e; --accent:#2f5bd3; }
  * { box-sizing: border-box; }
  body { margin:0; font:14px/1.45 system-ui, "Segoe UI", sans-serif; background:var(--bg); color:var(--ink); }
  header { position:sticky; top:0; z-index:5; background:var(--bg); border-bottom:1px solid var(--line);
           padding:12px 20px; display:flex; flex-wrap:wrap; gap:12px 20px; align-items:center; }
  h1 { font-size:17px; margin:0; }
  .stats { color:var(--muted); }
  .stats b { color:var(--ink); }
  .spacer { flex:1; }
  button { font:inherit; border:1px solid var(--line); background:#fff; border-radius:8px; padding:6px 12px; cursor:pointer; }
  button.primary { background:var(--accent); color:#fff; border-color:var(--accent); }
  button:disabled { opacity:.5; cursor:default; }
  .filters { width:100%; display:flex; flex-wrap:wrap; gap:6px; }
  .chip { border-radius:999px; padding:3px 10px; }
  .chip.on { background:var(--ink); color:#fff; border-color:var(--ink); }
  main { padding:16px 20px 80px; }
  .help { color:var(--muted); max-width:900px; margin:0 0 16px; }
  section { margin-bottom:28px; }
  section h2 { font-size:15px; margin:0 0 10px; display:flex; gap:12px; align-items:center; }
  section h2 .n { color:var(--muted); font-weight:normal; }
  .grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(220px, 1fr)); gap:12px; }
  .card { background:var(--card); border:2px solid var(--line); border-radius:10px; overflow:hidden; display:flex; flex-direction:column; }
  .card.confirmed { border-color:var(--ok); }
  .card.changed { border-color:var(--changed); }
  .thumb { height:170px; background:#eceae5; display:flex; align-items:center; justify-content:center; color:var(--muted);
           font-size:13px; text-decoration:none; }
  .thumb img { width:100%; height:100%; object-fit:cover; }
  .thumb .doc { font-size:28px; font-weight:600; letter-spacing:.5px; }
  .body { padding:8px 10px 10px; display:flex; flex-direction:column; gap:4px; flex:1; }
  .name { font-weight:600; word-break:break-all; font-size:12px; }
  .meta { color:var(--muted); font-size:12px; }
  .topic { font-size:12.5px; }
  .why { color:var(--muted); font-size:12px; font-style:italic; }
  .level-low, .level-medium { color:var(--low); font-weight:600; font-size:12px; }
  .level-medium { color:var(--changed); }
  .err { color:var(--low); font-size:12px; }
  select { font:inherit; width:100%; margin-top:auto; padding:5px; border-radius:6px; border:1px solid var(--line); }
  .hidden { display:none !important; }
  #toast { position:fixed; bottom:20px; left:50%; transform:translateX(-50%); background:var(--ink); color:#fff;
           padding:10px 16px; border-radius:8px; display:none; }
</style>
</head>
<body>
<header>
  <h1 id="title">Review</h1>
  <div class="stats" id="stats"></div>
  <div class="spacer"></div>
  <button class="primary" id="save">Save reviewed table</button>
  <div class="filters" id="filters"></div>
</header>
<main>
  <p class="help">Files are grouped by the suggested course. Look at each group, change the course of any
  file that is wrong, then press <b>Confirm group</b>. Green: confirmed. Orange: you changed it.
  Click a preview to open the file. When you are done, press <b>Save reviewed table</b> and save it over
  the old table. Only confirmed or changed files get a course.</p>
  <div id="groups"></div>
</main>
<div id="toast"></div>
<script id="data" type="application/json">)HTML";

constexpr const char* kPageBottom = R"HTML(</script>
<script>
"use strict";
const D = JSON.parse(document.getElementById("data").textContent);
const col = name => D.header.indexOf(name);
const C = { sha: col("sha256"), name: col("file_name"), type: col("type"), course: col("course"),
            sug: col("suggested_course"), level: col("suggestion_level"), kind: col("kind"), topic: col("topic"),
            why: col("evidence"), err: col("classify_error"), also: col("also_related"), when: col("captured_at"),
            pages: col("pages") };
const UNSURE = "";
const storeKey = "summarizer-review:" + D.table;
let saved = {};
try { saved = JSON.parse(localStorage.getItem(storeKey) || "{}"); } catch (e) { saved = {}; }

// state per row: choice (course or ""), confirmed (bool)
const state = D.rows.map(r => {
  const f = r.fields, s = saved[f[C.sha]];
  if (s) return { choice: s.choice, confirmed: s.confirmed };
  if (f[C.course]) return { choice: f[C.course], confirmed: true };
  return { choice: f[C.sug] || UNSURE, confirmed: false };
});
function persist() {
  const out = {};
  D.rows.forEach((r, i) => { out[r.fields[C.sha]] = state[i]; });
  try { localStorage.setItem(storeKey, JSON.stringify(out)); } catch (e) {}
}

function fileUrl(p) {
  if (!p) return "";
  const parts = p.replace(/\\/g, "/").split("/").map(encodeURIComponent);
  if (/^[A-Za-z]%3A$/.test(parts[0])) { parts[0] = parts[0].replace("%3A", ":"); return "file:///" + parts.join("/"); }
  return "file://" + parts.join("/");
}
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

const options = [...D.courses.map(c => [c, c]), ...D.special.map(s => [s, s]), [UNSURE, "Not sure / skip"]];
const groupOf = i => D.rows[i].fields[C.sug] || (D.rows[i].fields[C.err] ? "Could not classify" : "No suggestion yet");
const groupNames = [];
D.rows.forEach((r, i) => { const g = groupOf(i); if (!groupNames.includes(g)) groupNames.push(g); });
groupNames.sort((a, b) => (a.startsWith("(") || a.startsWith("No") || a.startsWith("Could")) - (b.startsWith("(") || b.startsWith("No") || b.startsWith("Could")) || a.localeCompare(b));

const cards = [];
const groupsEl = document.getElementById("groups");
const sections = {};
for (const g of groupNames) {
  const sec = el("section");
  const h = el("h2");
  h.append(el("span", "", g), el("span", "n", ""));
  const btn = el("button", "", "Confirm group");
  btn.onclick = () => { D.rows.forEach((r, i) => { if (groupOf(i) === g && !cards[i].classList.contains("hidden")) state[i].confirmed = true; }); persist(); render(); };
  h.append(btn);
  const grid = el("div", "grid");
  sec.append(h, grid);
  groupsEl.append(sec);
  sections[g] = { sec, grid, count: h.querySelector(".n") };
}

D.rows.forEach((r, i) => {
  const f = r.fields;
  const card = el("div", "card");
  const url = fileUrl(r.path);
  const thumb = el("a", "thumb");
  if (url) { thumb.href = url; thumb.target = "_blank"; }
  if (["jpeg", "png", "webp"].includes(f[C.type]) && url) {
    const img = el("img"); img.loading = "lazy"; img.src = url; img.alt = f[C.name];
    img.onerror = () => { thumb.textContent = "No preview"; };
    thumb.append(img);
  } else {
    thumb.append(el("div", "doc", (f[C.type] || "?").toUpperCase()));
  }
  const body = el("div", "body");
  body.append(el("div", "name", f[C.name]));
  const meta = [f[C.kind], f[C.when] ? f[C.when].replace("T", " ") : "", f[C.pages] ? f[C.pages] + (f[C.type] === "pptx" ? " slides" : " pages") : ""].filter(Boolean).join(" · ");
  if (meta) body.append(el("div", "meta", meta));
  if (f[C.topic]) body.append(el("div", "topic", f[C.topic]));
  if (f[C.level] && f[C.level] !== "high") body.append(el("div", "level-" + f[C.level], "Model is unsure (" + f[C.level] + ")"));
  if (f[C.also]) body.append(el("div", "meta", "Also related: " + f[C.also]));
  if (f[C.why]) body.append(el("div", "why", f[C.why]));
  if (f[C.err]) body.append(el("div", "err", f[C.err]));
  const sel = el("select");
  for (const [v, label] of options) { const o = el("option", "", label); o.value = v; sel.append(o); }
  if (!options.some(([v]) => v === state[i].choice)) { const o = el("option", "", state[i].choice); o.value = state[i].choice; sel.append(o); }
  sel.value = state[i].choice;
  sel.onchange = () => { state[i].choice = sel.value; state[i].confirmed = true; persist(); render(); };
  body.append(sel);
  card.append(thumb, body);
  sections[groupOf(i)].grid.append(card);
  cards.push(card);
});

let filter = "all";
function buildFilters() {
  const bar = document.getElementById("filters");
  bar.textContent = "";
  const items = [["all", "All"], ["todo", "Not confirmed"], ["unsure", "Model unsure"], ...groupNames.map(g => [g, g])];
  for (const [key, label] of items) {
    const b = el("button", "chip" + (filter === key ? " on" : ""), label);
    b.onclick = () => { filter = key; buildFilters(); render(); };
    bar.append(b);
  }
}
function visible(i) {
  const f = D.rows[i].fields;
  if (filter === "all") return true;
  if (filter === "todo") return !state[i].confirmed;
  if (filter === "unsure") return (f[C.level] && f[C.level] !== "high") || !!f[C.err];
  return groupOf(i) === filter;
}
function render() {
  let confirmed = 0, changed = 0;
  D.rows.forEach((r, i) => {
    const sug = r.fields[C.sug] || UNSURE;
    const isChanged = state[i].confirmed && state[i].choice !== sug;
    cards[i].classList.toggle("confirmed", state[i].confirmed && !isChanged);
    cards[i].classList.toggle("changed", isChanged);
    cards[i].classList.toggle("hidden", !visible(i));
    confirmed += state[i].confirmed ? 1 : 0;
    changed += isChanged ? 1 : 0;
  });
  for (const g of groupNames) {
    const idx = D.rows.map((r, i) => i).filter(i => groupOf(i) === g);
    const shown = idx.filter(i => visible(i)).length;
    sections[g].sec.classList.toggle("hidden", shown === 0);
    sections[g].count.textContent = idx.filter(i => state[i].confirmed).length + " / " + idx.length + " confirmed";
  }
  document.getElementById("stats").innerHTML = "";
  const s = document.getElementById("stats");
  s.append(el("b", "", String(D.rows.length)), " files · ", el("b", "", String(confirmed)), " confirmed · ",
           el("b", "", String(changed)), " changed");
}

function csvCell(v) {
  return /["\r\n]/.test(v) || v.includes(D.separator) ? '"' + v.replace(/"/g, '""') + '"' : v;
}
function buildCsv() {
  const lines = [D.header.map(csvCell).join(D.separator)];
  D.rows.forEach((r, i) => {
    const f = r.fields.slice();
    if (state[i].confirmed) f[C.course] = state[i].choice;
    lines.push(f.map(csvCell).join(D.separator));
  });
  return "﻿" + lines.join("\r\n") + "\r\n";
}
function toast(msg) {
  const t = document.getElementById("toast"); t.textContent = msg; t.style.display = "block";
  setTimeout(() => { t.style.display = "none"; }, 4000);
}
document.getElementById("save").onclick = async () => {
  const blob = new Blob([buildCsv()], { type: "text/csv;charset=utf-8" });
  if (window.showSaveFilePicker) {
    try {
      const handle = await window.showSaveFilePicker({ suggestedName: D.table,
        types: [{ description: "CSV table", accept: { "text/csv": [".csv"] } }] });
      const w = await handle.createWritable(); await w.write(blob); await w.close();
      toast("Saved. Run \"summarizer report\" on the table to see the accuracy.");
      return;
    } catch (e) { if (e && e.name === "AbortError") return; }
  }
  const a = el("a"); a.href = URL.createObjectURL(blob); a.download = D.table; a.click();
  toast("Downloaded " + D.table + ". Move it over the old table.");
};

document.getElementById("title").textContent = "Review: " + D.title;
buildFilters();
render();
</script>
</body>
</html>
)HTML";

}  // namespace

std::string build_review_page(const ReviewData& data) {
    std::string json = "{\"title\":" + json_str(data.title) + ",\"table\":" + json_str(data.table_file_name) +
                       ",\"separator\":" + json_str(std::string(1, data.separator)) +
                       ",\"header\":" + json_array(data.header) + ",\"courses\":" + json_array(data.courses) +
                       ",\"special\":" + json_array(data.special) + ",\"rows\":[";
    for (std::size_t i = 0; i < data.rows.size(); ++i) {
        json += (i ? "," : "") + std::string("{\"path\":") + json_str(data.rows[i].path) +
                ",\"fields\":" + json_array(data.rows[i].fields) + "}";
    }
    json += "]}";
    // File names and model text must not be able to close the script element.
    std::string safe;
    safe.reserve(json.size());
    for (const char c : json) {
        if (c == '<') {
            safe += "\\u003c";
        } else {
            safe += c;
        }
    }
    return std::string(kPageTop) + safe + kPageBottom;
}

}  // namespace cli
