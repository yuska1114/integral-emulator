# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Authenticated administration pages and login form."""

import json


ADMIN_LOGIN_HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>INTEGRAL EMULATOR Admin Login</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #eef2ee;
      --surface: #ffffff;
      --ink: #17201b;
      --muted: #5f6d66;
      --line: #d4ddd6;
      --accent: #1e7358;
      --accent-dark: #14533f;
    }
    * { box-sizing: border-box; }
    body {
      min-height: 100vh;
      margin: 0;
      display: grid;
      place-items: center;
      background: var(--bg);
      color: var(--ink);
      font: 14px/1.45 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    main {
      width: min(420px, calc(100vw - 32px));
      background: var(--surface);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 18px;
      box-shadow: 0 1px 2px rgba(16, 24, 20, .08);
    }
    h1 {
      margin: 0 0 16px;
      font-size: 20px;
      font-weight: 800;
      letter-spacing: 0;
    }
    label {
      display: grid;
      gap: 6px;
      color: var(--muted);
      font-size: 12px;
      font-weight: 800;
    }
    input {
      min-height: 38px;
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 7px 10px;
      font: inherit;
      color: var(--ink);
      background: #fff;
    }
    button {
      width: 100%;
      min-height: 38px;
      margin-top: 12px;
      border: 1px solid var(--accent-dark);
      border-radius: 6px;
      background: var(--accent);
      color: white;
      font: inherit;
      font-weight: 800;
      cursor: pointer;
    }
    button:disabled { cursor: wait; opacity: .62; }
    .status {
      min-height: 22px;
      margin-top: 10px;
      color: #8c3325;
      font-size: 12px;
      font-weight: 800;
    }
  </style>
</head>
<body>
  <main>
    <h1>INTEGRAL EMULATOR Admin</h1>
    <form id="loginForm">
      <label>Password
        <input id="password" type="password" autocomplete="current-password" autofocus>
      </label>
      <button id="loginButton">Login</button>
      <div id="status" class="status"></div>
    </form>
  </main>
  <script>
    const ADMIN_BASE_PATH = __ADMIN_BASE_PATH__;
    const adminUrl = suffix => `${ADMIN_BASE_PATH}${suffix}`;
    const form = document.querySelector("#loginForm");
    const passwordEl = document.querySelector("#password");
    const button = document.querySelector("#loginButton");
    const statusEl = document.querySelector("#status");

    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      button.disabled = true;
      statusEl.textContent = "";
      try {
        const response = await fetch(adminUrl("/session"), {
          method: "POST",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify({password: passwordEl.value})
        });
        const payload = await response.json();
        if (!response.ok) throw new Error(payload?.error?.message || "login failed");
        location.href = ADMIN_BASE_PATH;
      }
      catch (error) {
        statusEl.textContent = error.message;
      }
      finally {
        button.disabled = false;
      }
    });
  </script>
</body>
</html>
"""


ADMIN_HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>INTEGRAL EMULATOR Admin</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #eef2ee;
      --surface: #ffffff;
      --ink: #17201b;
      --muted: #5f6d66;
      --line: #d4ddd6;
      --accent: #1e7358;
      --accent-dark: #14533f;
      --warn: #ad5d2a;
      --shadow: 0 1px 2px rgba(16, 24, 20, .08);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: var(--bg);
      color: var(--ink);
      font: 14px/1.45 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    header {
      min-height: 64px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      padding: 14px 20px;
      background: #fbfcf8;
      border-bottom: 1px solid var(--line);
    }
    h1 {
      margin: 0;
      font-size: 20px;
      font-weight: 800;
      letter-spacing: 0;
    }
    .header-actions {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
      justify-content: flex-end;
    }
    main {
      max-width: 1240px;
      margin: 0 auto;
      padding: 16px;
      display: grid;
      gap: 14px;
    }
    .tabs {
      display: flex;
      gap: 8px;
      border-bottom: 1px solid var(--line);
      padding: 0 0 10px;
    }
    .tab-button {
      background: #fff;
      color: var(--ink);
      border-color: var(--line);
    }
    .tab-button.on {
      background: var(--accent);
      color: #fff;
      border-color: var(--accent-dark);
    }
    .tab-panel { display: none; }
    .tab-panel.on { display: block; }
    section {
      background: var(--surface);
      border: 1px solid var(--line);
      border-radius: 8px;
      box-shadow: var(--shadow);
      padding: 14px;
    }
    h2 {
      margin: 0 0 12px;
      font-size: 14px;
      font-weight: 800;
    }
    .toolbar {
      display: flex;
      align-items: end;
      gap: 10px;
      flex-wrap: wrap;
    }
    label {
      display: grid;
      gap: 5px;
      min-width: 220px;
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
    }
    input {
      min-height: 34px;
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 6px 9px;
      font: inherit;
      color: var(--ink);
      background: #fff;
    }
    input[type="file"] {
      padding: 5px 8px;
    }
    button {
      min-height: 34px;
      border: 1px solid var(--accent-dark);
      border-radius: 6px;
      background: var(--accent);
      color: white;
      padding: 0 12px;
      font: inherit;
      font-weight: 800;
      cursor: pointer;
    }
    button.secondary {
      background: #fff;
      color: var(--ink);
      border-color: var(--line);
    }
    button:disabled {
      cursor: wait;
      opacity: .62;
    }
    .status {
      display: inline-flex;
      align-items: center;
      min-height: 28px;
      padding: 0 10px;
      border: 1px solid var(--line);
      border-radius: 999px;
      background: #fff;
      color: var(--muted);
      font-size: 12px;
      font-weight: 800;
    }
    .status.ok { color: var(--accent-dark); border-color: #9bc6b8; }
    .issued {
      display: none;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 8px;
      margin-top: 12px;
    }
    .issued.on { display: grid; }
    .tool-divider {
      height: 1px;
      background: var(--line);
      margin: 16px 0 14px;
    }
    .field {
      min-height: 58px;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 9px;
      background: #fbfcf8;
    }
    .field span {
      display: block;
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
      margin-bottom: 3px;
    }
    .field strong {
      display: block;
      font-size: 16px;
      word-break: break-word;
    }
    table {
      width: 100%;
      border-collapse: collapse;
    }
    th, td {
      padding: 9px 8px;
      border-bottom: 1px solid var(--line);
      text-align: left;
      vertical-align: top;
    }
    th {
      color: var(--muted);
      font-size: 12px;
      font-weight: 800;
      background: #fbfcf8;
    }
    tr.user-row { cursor: pointer; }
    tr.user-row:hover { background: #f7faf6; }
    .mono {
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 12px;
      word-break: break-word;
    }
    .pill {
      display: inline-flex;
      align-items: center;
      min-height: 24px;
      padding: 0 8px;
      border: 1px solid #bdd4ca;
      border-radius: 999px;
      background: #f3f8f5;
      color: var(--accent-dark);
      font-size: 12px;
      font-weight: 800;
      margin: 0 4px 4px 0;
    }
    .empty { color: var(--muted); }
    .detail {
      display: none;
      background: #fbfcf8;
    }
    .detail.on { display: table-row; }
    .detail-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 10px;
    }
    .ops-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
    }
    .ops-grid .wide { grid-column: 1 / -1; }
    .detail-block {
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #fff;
      padding: 10px;
      min-width: 0;
    }
    .detail-block h3 {
      margin: 0 0 8px;
      font-size: 13px;
    }
    pre {
      margin: 0;
      max-height: 260px;
      overflow: auto;
      white-space: pre-wrap;
      word-break: break-word;
      font: 12px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      color: #203028;
    }
    @media (max-width: 860px) {
      .issued, .detail-grid, .ops-grid { grid-template-columns: 1fr; }
      .ops-grid .wide { grid-column: auto; }
      header { align-items: flex-start; flex-direction: column; }
      table { min-width: 860px; }
      .table-wrap { overflow-x: auto; }
    }
  </style>
</head>
<body>
  <header>
    <h1>INTEGRAL EMULATOR Admin</h1>
    <div class="header-actions">
      <span id="status" class="status">loading</span>
      <button id="logoutButton" class="secondary" type="button">Logout</button>
    </div>
  </header>
  <main>
    <div class="tabs">
      <button id="issueTab" class="tab-button on" type="button">Issue ID</button>
      <button id="usersTab" class="tab-button" type="button">User Data</button>
      <button id="opsTab" class="tab-button" type="button">Operations</button>
      <button id="logsTab" class="tab-button" type="button">Server Logs</button>
    </div>
    <section id="issuePanel" class="tab-panel on">
      <h2>Issue ID</h2>
      <div class="toolbar">
        <label>Username
          <input id="prefix" value="player001">
        </label>
        <label>Email
          <input id="issueEmail" value="" type="email">
        </label>
        <button id="issueButton">Issue</button>
      </div>
      <div id="issued" class="issued">
        <div class="field"><span>Username</span><strong id="issuedUsername">-</strong></div>
        <div class="field"><span>Email</span><strong id="issuedEmail">-</strong></div>
        <div class="field"><span>Password</span><strong id="issuedPassword">-</strong></div>
        <div class="field"><span>User ID</span><strong id="issuedUserId" class="mono">-</strong></div>
      </div>
      <div class="tool-divider"></div>
      <h2>Password Reset</h2>
      <div class="toolbar">
        <label>Username
          <input id="resetUsername" value="">
        </label>
        <button id="resetButton" class="secondary">Reset</button>
      </div>
      <div id="resetResult" class="issued">
        <div class="field"><span>Username</span><strong id="resetResultUsername">-</strong></div>
        <div class="field"><span>New Password</span><strong id="resetResultPassword">-</strong></div>
        <div class="field"><span>User ID</span><strong id="resetResultUserId" class="mono">-</strong></div>
      </div>
    </section>
    <section id="usersPanel" class="tab-panel">
      <div class="toolbar">
        <h2 style="margin-right:auto">Users</h2>
        <button id="refreshButton" class="secondary">Refresh</button>
      </div>
      <div class="tool-divider"></div>
      <h2>SAV Replace</h2>
      <div class="toolbar">
        <label>SAV ID
          <input id="replaceSaveId" value="" placeholder="save_...">
        </label>
        <label>SAV File
          <input id="replaceSaveFile" type="file" accept=".sav,application/octet-stream">
        </label>
        <button id="replaceSaveButton" class="secondary" type="button">Replace SAV</button>
      </div>
      <div class="tool-divider"></div>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Username</th>
              <th>Email</th>
              <th>Status</th>
              <th>ROM Slots</th>
              <th>SAV</th>
              <th>ROM</th>
              <th>Sessions</th>
              <th>Created</th>
            </tr>
          </thead>
          <tbody id="users"></tbody>
        </table>
      </div>
    </section>
    <section id="opsPanel" class="tab-panel">
      <div class="toolbar">
        <h2 style="margin-right:auto">Operations</h2>
        <button id="opsRefreshButton" class="secondary">Refresh</button>
      </div>
      <div class="ops-grid">
        <div class="detail-block wide">
          <h3>Session Lifecycle</h3>
          <pre id="opsLifecycle">-</pre>
        </div>
        <div class="detail-block wide">
          <h3>Rooms</h3>
          <pre id="opsRooms">-</pre>
        </div>
        <div class="detail-block">
          <h3>Host Processes</h3>
          <pre id="opsHosts">-</pre>
        </div>
        <div class="detail-block">
          <h3>Recent Sessions</h3>
          <pre id="opsSessions">-</pre>
        </div>
        <div class="detail-block wide">
          <h3>Recent Events</h3>
          <pre id="opsEvents">-</pre>
        </div>
      </div>
    </section>
    <section id="logsPanel" class="tab-panel">
      <div class="toolbar">
        <h2 style="margin-right:auto">Server Logs</h2>
        <button id="logsRefreshButton" class="secondary" type="button">Refresh</button>
        <button id="logsDownloadButton" type="button">Download Log</button>
      </div>
      <div class="tool-divider"></div>
      <div class="detail-block">
        <h3 id="logsMeta">Latest logs</h3>
        <pre id="serverLogs">-</pre>
      </div>
    </section>
  </main>
  <script>
    const ADMIN_BASE_PATH = __ADMIN_BASE_PATH__;
    const adminUrl = suffix => `${ADMIN_BASE_PATH}${suffix}`;
    const statusEl = document.querySelector("#status");
    const logoutButton = document.querySelector("#logoutButton");
    const issueTab = document.querySelector("#issueTab");
    const usersTab = document.querySelector("#usersTab");
    const opsTab = document.querySelector("#opsTab");
    const logsTab = document.querySelector("#logsTab");
    const issuePanel = document.querySelector("#issuePanel");
    const usersPanel = document.querySelector("#usersPanel");
    const opsPanel = document.querySelector("#opsPanel");
    const logsPanel = document.querySelector("#logsPanel");
    const prefixEl = document.querySelector("#prefix");
    const issueEmailEl = document.querySelector("#issueEmail");
    const resetUsernameEl = document.querySelector("#resetUsername");
    const issueButton = document.querySelector("#issueButton");
    const resetButton = document.querySelector("#resetButton");
    const refreshButton = document.querySelector("#refreshButton");
    const opsRefreshButton = document.querySelector("#opsRefreshButton");
    const logsRefreshButton = document.querySelector("#logsRefreshButton");
    const logsDownloadButton = document.querySelector("#logsDownloadButton");
    const replaceSaveIdEl = document.querySelector("#replaceSaveId");
    const replaceSaveFileEl = document.querySelector("#replaceSaveFile");
    const replaceSaveButton = document.querySelector("#replaceSaveButton");
    const usersEl = document.querySelector("#users");
    const opsRooms = document.querySelector("#opsRooms");
    const opsLifecycle = document.querySelector("#opsLifecycle");
    const opsHosts = document.querySelector("#opsHosts");
    const opsSessions = document.querySelector("#opsSessions");
    const opsEvents = document.querySelector("#opsEvents");
    const logsMeta = document.querySelector("#logsMeta");
    const serverLogs = document.querySelector("#serverLogs");
    const issuedEl = document.querySelector("#issued");
    const issuedUsername = document.querySelector("#issuedUsername");
    const issuedEmail = document.querySelector("#issuedEmail");
    const issuedPassword = document.querySelector("#issuedPassword");
    const issuedUserId = document.querySelector("#issuedUserId");
    const resetResultEl = document.querySelector("#resetResult");
    const resetResultUsername = document.querySelector("#resetResultUsername");
    const resetResultPassword = document.querySelector("#resetResultPassword");
    const resetResultUserId = document.querySelector("#resetResultUserId");
    let cachedUsers = [];
    let usersLoaded = false;
    let opsLoaded = false;
    let logsLoaded = false;

    function setStatus(text, ok = true) {
      statusEl.textContent = text;
      statusEl.className = ok ? "status ok" : "status";
    }

    async function api(method, path, body) {
      const response = await fetch(adminUrl(path), {
        method,
        headers: {"Content-Type": "application/json"},
        body: body === undefined ? undefined : JSON.stringify(body)
      });
      const payload = await response.json();
      if (!response.ok) {
        if (response.status === 401) location.href = ADMIN_BASE_PATH;
        throw new Error(payload?.error?.message || response.statusText);
      }
      return payload;
    }

    function fmtDate(value) {
      if (!value) return "-";
      const date = new Date(value);
      return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
    }

    function slotPills(slots) {
      if (!slots.length) return '<span class="empty">EMPTY</span>';
      return slots.map(slot => {
        const label = slot.rom_id ? `SLOT${slot.slot} ${slot.game_type || ""}` : `SLOT${slot.slot} EMPTY`;
        return `<span class="pill">${escapeHtml(label)}</span>`;
      }).join("");
    }

    function escapeHtml(value) {
      return String(value ?? "").replace(/[&<>"']/g, char => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"
      })[char]);
    }

    function bytesToBase64(bytes) {
      let binary = "";
      const chunkSize = 0x8000;
      for (let i = 0; i < bytes.length; i += chunkSize) {
        binary += String.fromCharCode(...bytes.subarray(i, i + chunkSize));
      }
      return btoa(binary);
    }

    function renderUsers(users) {
      cachedUsers = users;
      usersEl.innerHTML = "";
      if (!users.length) {
        usersEl.innerHTML = '<tr><td colspan="8" class="empty">NO USERS</td></tr>';
        return;
      }
      users.forEach((item, index) => {
        const user = item.user;
        const row = document.createElement("tr");
        row.className = "user-row";
        row.innerHTML = `
          <td><strong>${escapeHtml(user.username)}</strong><div class="mono">${escapeHtml(user.id)}</div></td>
          <td>${escapeHtml(user.email || "-")}</td>
          <td>${escapeHtml(user.status)}</td>
          <td>${item.counts.rom_slots} / 8</td>
          <td>${item.counts.saves}</td>
          <td>${item.counts.rom_registrations}</td>
          <td>${item.counts.link_sessions}</td>
          <td>${escapeHtml(fmtDate(user.created_at))}</td>
        `;
        const detail = document.createElement("tr");
        detail.className = "detail";
        detail.innerHTML = `
          <td colspan="8">
            <div class="detail-grid">
              <div class="detail-block">
                <h3>ROM Slots</h3>
                <div>${slotPills(item.rom_slots)}</div>
                <pre>${escapeHtml(JSON.stringify(item.rom_slots, null, 2))}</pre>
              </div>
              <div class="detail-block">
                <h3>SAV</h3>
                <pre>${escapeHtml(JSON.stringify(item.saves, null, 2))}</pre>
              </div>
              <div class="detail-block">
                <h3>ROM / Sessions</h3>
                <pre>${escapeHtml(JSON.stringify({rom_registrations: item.rom_registrations, link_sessions: item.link_sessions}, null, 2))}</pre>
              </div>
            </div>
          </td>
        `;
        row.addEventListener("click", () => detail.classList.toggle("on"));
        usersEl.append(row, detail);
      });
    }

    async function refresh() {
      refreshButton.disabled = true;
      try {
        const result = await api("GET", "/users");
        renderUsers(result.users);
        usersLoaded = true;
        setStatus(`${result.users.length} users`);
      }
      catch (error) {
        setStatus(error.message, false);
      }
      finally {
        refreshButton.disabled = false;
      }
    }

    async function replaceSave() {
      replaceSaveButton.disabled = true;
      try {
        const saveId = replaceSaveIdEl.value.trim();
        const file = replaceSaveFileEl.files[0];
        if (!saveId) throw new Error("SAV ID is required");
        if (!file) throw new Error("SAV file is required");
        const bytes = new Uint8Array(await file.arrayBuffer());
        const result = await api("POST", `/saves/${encodeURIComponent(saveId)}/replace`, {
          save_data: bytesToBase64(bytes)
        });
        usersLoaded = false;
        await refresh();
        setStatus(`SAV replaced rev ${result.save.revision}`);
      }
      catch (error) {
        setStatus(error.message, false);
      }
      finally {
        replaceSaveButton.disabled = false;
      }
    }

    function summarizeRooms(rooms) {
      return rooms.map(room => ({
        room: room.room_number,
        status: room.room_status,
        session: room.link_session_id || "-",
        session_status: room.session_status || "-",
        host: room.host_process_id || "-",
        users: room.users.map(user => `${user.username || "USER"} ${user.slot || "-"}${user.ready ? " READY" : ""}`)
      }));
    }

    async function refreshOps() {
      opsRefreshButton.disabled = true;
      try {
        const result = await api("GET", "/operations");
        opsLifecycle.textContent = JSON.stringify(result.lifecycle, null, 2);
        opsRooms.textContent = JSON.stringify(summarizeRooms(result.rooms), null, 2);
        opsHosts.textContent = JSON.stringify(result.host_processes, null, 2);
        opsSessions.textContent = JSON.stringify(result.link_sessions, null, 2);
        opsEvents.textContent = JSON.stringify(result.events, null, 2);
        opsLoaded = true;
        setStatus("operations refreshed");
      }
      catch (error) {
        setStatus(error.message, false);
      }
      finally {
        opsRefreshButton.disabled = false;
      }
    }

    async function refreshLogs() {
      logsRefreshButton.disabled = true;
      try {
        const result = await api("GET", "/logs");
        const lines = result.log.lines || [];
        logsMeta.textContent = `${lines.length} lines`;
        serverLogs.textContent = lines.length ? lines.join("\\n") : "NO LOGS";
        logsLoaded = true;
        setStatus("logs refreshed");
      }
      catch (error) {
        setStatus(error.message, false);
      }
      finally {
        logsRefreshButton.disabled = false;
      }
    }

    function downloadLogs() {
      location.href = adminUrl("/logs/download");
    }

    async function issueUser() {
      issueButton.disabled = true;
      try {
        const result = await api("POST", "/users/issue", {username: prefixEl.value, email: issueEmailEl.value});
        issuedUsername.textContent = result.user.username;
        issuedEmail.textContent = result.user.email || "-";
        issuedPassword.textContent = result.initial_password;
        issuedUserId.textContent = result.user.id;
        issuedEl.classList.add("on");
        resetUsernameEl.value = result.user.username;
        usersLoaded = false;
        setStatus("issued");
      }
      catch (error) {
        setStatus(error.message, false);
      }
      finally {
        issueButton.disabled = false;
      }
    }

    async function resetPassword() {
      resetButton.disabled = true;
      try {
        const result = await api("POST", "/users/reset-password", {username: resetUsernameEl.value});
        resetResultUsername.textContent = result.user.username;
        resetResultPassword.textContent = result.initial_password;
        resetResultUserId.textContent = result.user.id;
        resetResultEl.classList.add("on");
        usersLoaded = false;
        setStatus("password reset");
      }
      catch (error) {
        setStatus(error.message, false);
      }
      finally {
        resetButton.disabled = false;
      }
    }

    function switchTab(target) {
      const showUsers = target === "users";
      const showOps = target === "ops";
      const showLogs = target === "logs";
      issueTab.classList.toggle("on", !showUsers && !showOps && !showLogs);
      usersTab.classList.toggle("on", showUsers);
      opsTab.classList.toggle("on", showOps);
      logsTab.classList.toggle("on", showLogs);
      issuePanel.classList.toggle("on", !showUsers && !showOps && !showLogs);
      usersPanel.classList.toggle("on", showUsers);
      opsPanel.classList.toggle("on", showOps);
      logsPanel.classList.toggle("on", showLogs);
      if (showUsers && !usersLoaded) {
        refresh();
      }
      if (showOps && !opsLoaded) {
        refreshOps();
      }
      if (showLogs && !logsLoaded) {
        refreshLogs();
      }
    }

    issueTab.addEventListener("click", () => switchTab("issue"));
    usersTab.addEventListener("click", () => switchTab("users"));
    opsTab.addEventListener("click", () => switchTab("ops"));
    logsTab.addEventListener("click", () => switchTab("logs"));
    issueButton.addEventListener("click", issueUser);
    resetButton.addEventListener("click", resetPassword);
    refreshButton.addEventListener("click", refresh);
    opsRefreshButton.addEventListener("click", refreshOps);
    logsRefreshButton.addEventListener("click", refreshLogs);
    logsDownloadButton.addEventListener("click", downloadLogs);
    replaceSaveButton.addEventListener("click", replaceSave);
    logoutButton.addEventListener("click", async () => {
      logoutButton.disabled = true;
      await api("POST", "/logout", {}).catch(() => undefined);
      location.href = ADMIN_BASE_PATH;
    });
    setStatus("ready");
  </script>
</body>
</html>
"""


def render_admin_login_html(admin_base_path: str) -> str:
    return ADMIN_LOGIN_HTML.replace(
        "__ADMIN_BASE_PATH__", json.dumps(admin_base_path)
    )


def render_admin_html(admin_base_path: str) -> str:
    return ADMIN_HTML.replace("__ADMIN_BASE_PATH__", json.dumps(admin_base_path))
