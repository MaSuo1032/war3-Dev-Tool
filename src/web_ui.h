#ifndef WEB_UI_H
#define WEB_UI_H

const char HTML_UI[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Warcraft III 1.27a Master Unit Controller</title>
    <style>
        :root {
            --bg-base: #0b0c10;
            --bg-card: #141720;
            --bg-card-hover: #1c212d;
            --border-color: #242b3d;
            --accent-primary: #00ff77;
            --accent-primary-dim: rgba(0, 255, 119, 0.12);
            --accent-danger: #ff3355;
            --accent-mana: #00b0ff;
            --accent-mana-dim: rgba(0, 176, 255, 0.12);
            --accent-warn: #ffaa00;
            --accent-gold: #ffd54a;
            --accent-lumber: #8fd36a;
            --text-main: #f0f3f8;
            --text-muted: #7b8499;
            --font-mono: 'Consolas', 'Fira Code', monospace;
            --font-sans: 'Segoe UI', system-ui, -apple-system, sans-serif;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            background-color: var(--bg-base);
            color: var(--text-main);
            font-family: var(--font-sans);
            padding: 24px;
            display: flex;
            justify-content: center;
        }
        .container { width: 100%; max-width: 1180px; display: flex; flex-direction: column; gap: 16px; }
        .header-bar {
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 14px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .title-group h1 { font-size: 1.15rem; font-weight: 700; color: #fff; }
        .title-group p { font-size: 0.8rem; color: var(--text-muted); font-family: var(--font-mono); }
        .status-badge {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            font-family: var(--font-mono);
            font-size: 0.85rem;
            padding: 6px 14px;
            border-radius: 9999px;
            border: 1px solid var(--border-color);
            background: #0d0f15;
        }
        .status-dot { width: 8px; height: 8px; border-radius: 50%; background-color: var(--accent-danger); box-shadow: 0 0 8px var(--accent-danger); }
        .status-dot.active { background-color: var(--accent-primary); box-shadow: 0 0 8px var(--accent-primary); }
        .action-bar { display: flex; gap: 10px; flex-wrap: wrap; }
        .btn {
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            color: var(--text-main);
            padding: 8px 16px;
            font-size: 0.85rem;
            font-weight: 600;
            border-radius: 6px;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            transition: all 0.2s;
        }
        .btn:hover { background: var(--bg-card-hover); border-color: #3b4663; }
        .btn-primary { background: var(--accent-primary-dim); border-color: var(--accent-primary); color: var(--accent-primary); }
        .btn-danger { background: rgba(255, 51, 85, 0.12); border-color: var(--accent-danger); color: var(--accent-danger); }
        .btn-gold { background: rgba(255, 213, 74, 0.10); border-color: var(--accent-gold); color: var(--accent-gold); }
        .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
        .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
        .grid-4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; }
        .grid-resources { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
        @media (max-width: 900px) {
            .grid-2, .grid-3, .grid-4, .grid-resources { grid-template-columns: 1fr; }
        }
        .card {
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 16px;
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        .card-header {
            font-size: 0.85rem;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 0.8px;
            color: var(--text-muted);
            display: flex;
            justify-content: space-between;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 8px;
        }
        .gauge-block { display: flex; flex-direction: column; gap: 4px; }
        .gauge-header { display: flex; justify-content: space-between; font-family: var(--font-mono); font-size: 0.8rem; }
        .gauge-track { width: 100%; height: 10px; background: #0d0f15; border-radius: 3px; overflow: hidden; border: 1px solid var(--border-color); }
        .gauge-fill { height: 100%; width: 0%; transition: width 0.2s ease; }
        .gauge-hp { background: linear-gradient(90deg, #00aa44, var(--accent-primary)); }
        .gauge-mp { background: linear-gradient(90deg, #0077aa, var(--accent-mana)); }
        .stat-box {
            background: #0d0f15;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 8px 10px;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .stat-label { font-size: 0.72rem; color: var(--text-muted); font-weight: 600; text-transform: uppercase; }
        .stat-input-group { display: flex; gap: 4px; }
        .stat-input-group input {
            width: 100%;
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            color: #fff;
            padding: 4px 6px;
            font-family: var(--font-mono);
            font-size: 0.85rem;
            border-radius: 4px;
            outline: none;
        }
        .stat-input-group input:focus { border-color: var(--accent-mana); }
        .stat-btn {
            background: var(--border-color);
            border: none;
            color: var(--text-main);
            padding: 0 8px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 0.75rem;
            font-weight: bold;
        }
        .stat-btn:hover { background: #3b4663; }
)rawliteral"
R"rawliteral(
        .resource-card {
            background: #0d0f15;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 10px;
            display: flex;
            flex-direction: column;
            gap: 8px;
        }
        .resource-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-family: var(--font-mono);
            font-size: 0.78rem;
            color: var(--text-muted);
        }
        .resource-values { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
        .resource-value {
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            border-radius: 5px;
            padding: 7px 8px;
        }
        .resource-value .label { font-size: 0.68rem; color: var(--text-muted); text-transform: uppercase; }
        .resource-value .value { margin-top: 2px; font-family: var(--font-mono); font-size: 1rem; font-weight: 700; }
        .resource-value.gold .value { color: var(--accent-gold); }
        .resource-value.lumber .value { color: var(--accent-lumber); }
        .resource-edit { display: grid; grid-template-columns: 1fr auto; gap: 4px; }
        .resource-edit input {
            width: 100%;
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            color: #fff;
            padding: 5px 6px;
            font-family: var(--font-mono);
            font-size: 0.8rem;
            border-radius: 4px;
            outline: none;
        }
        .resource-edit input:focus { border-color: var(--accent-gold); }
        .item-slot {
            background: #0d0f15;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 8px;
            display: flex;
            flex-direction: column;
            gap: 6px;
        }
        .item-slot-header { display: flex; justify-content: space-between; font-size: 0.75rem; color: var(--accent-warn); font-family: var(--font-mono); }
        .console-box {
            background: #07080a;
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 10px 14px;
            font-family: var(--font-mono);
            font-size: 0.75rem;
            color: #b0b8c4;
            height: 90px;
            overflow-y: auto;
            display: flex;
            flex-direction: column;
            gap: 2px;
        }
        .log-entry { line-height: 1.3; }
        .log-time { color: #485266; margin-right: 6px; }
        .log-success { color: var(--accent-primary); }
        .log-warn { color: var(--accent-warn); }
        .log-error { color: var(--accent-danger); }
        .resource-toolbar { display: flex; gap: 8px; align-items: center; }
        .resource-summary { color: var(--text-muted); font-size: 0.75rem; font-family: var(--font-mono); }
    </style>
</head>
<body>
<div class="container">
    <header class="header-bar">
        <div class="title-group">
            <h1>WARCRAFT III MASTER CONTROLLER</h1>
            <p>Target: 1.27a (1.27.0.52240) Full Unit Structure Mod</p>
        </div>
        <div class="status-badge">
            <div id="statusDot" class="status-dot"></div>
            <span id="statusText">DISCONNECTED</span>
        </div>
    </header>

    <div class="action-bar">
        <button class="btn btn-primary" onclick="rehookGame()">[R] Hook / Rebind DLL</button>
        <button class="btn" onclick="fetchSelectedUnit()">[S] Scan Selected Unit</button>
        <button class="btn btn-gold" onclick="fetchResources()">[G] Scan Gold / Lumber</button>
        <button class="btn btn-danger" id="godModeBtn" onclick="toggleGodMode()">God Mode: OFF</button>
    </div>

    <!-- Section 0: Player Resources -->
    <div class="card">
        <div class="card-header">
            <span>Player Resources</span>
            <div class="resource-toolbar">
                <span class="resource-summary" id="resourceSummary">0 / 12 players resolved</span>
            </div>
        </div>
        <div class="grid-resources" id="resourcesGrid"></div>
    </div>

    <!-- Section 1: Core Health & Mana -->
    <div class="card">
        <div class="card-header">
            <span>Vital Statistics & Pools</span>
            <span id="targetAddr" style="font-family: var(--font-mono); color: var(--accent-mana);">0x00000000</span>
        </div>
        <div class="grid-2">
            <div class="gauge-block">
                <div class="gauge-header"><span>HEALTH (HP)</span><span id="hpValues">0.0 / 0.0</span></div>
                <div class="gauge-track"><div id="hpFill" class="gauge-fill gauge-hp"></div></div>
            </div>
            <div class="gauge-block">
                <div class="gauge-header"><span>MANA (MP)</span><span id="mpValues">0.0 / 0.0</span></div>
                <div class="gauge-track"><div id="mpFill" class="gauge-fill gauge-mp"></div></div>
            </div>
        </div>
        <div class="grid-4">
            <div class="stat-box"><span class="stat-label">Current HP</span><div class="stat-input-group"><input type="number" id="in_hp" placeholder="0"><button class="stat-btn" onclick="writeStat('hp', 'in_hp')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Max HP</span><div class="stat-input-group"><input type="number" id="in_maxHp" placeholder="0"><button class="stat-btn" onclick="writeStat('maxHp', 'in_maxHp')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">HP Regen (/s)</span><div class="stat-input-group"><input type="number" step="0.1" id="in_hpRegen" placeholder="0.0"><button class="stat-btn" onclick="writeStat('hpRegen', 'in_hpRegen')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Defense</span><div class="stat-input-group"><input type="number" step="0.1" id="in_defense" placeholder="0.0"><button class="stat-btn" onclick="writeStat('defense', 'in_defense')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Current MP</span><div class="stat-input-group"><input type="number" id="in_mp" placeholder="0"><button class="stat-btn" onclick="writeStat('mp', 'in_mp')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Max MP</span><div class="stat-input-group"><input type="number" id="in_maxMp" placeholder="0"><button class="stat-btn" onclick="writeStat('maxMp', 'in_maxMp')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">MP Regen (/s)</span><div class="stat-input-group"><input type="number" step="0.1" id="in_mpRegen" placeholder="0.0"><button class="stat-btn" onclick="writeStat('mpRegen', 'in_mpRegen')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Move Speed</span><div class="stat-input-group"><input type="number" step="1" id="in_moveSpeed" placeholder="522.0"><button class="stat-btn" onclick="writeStat('moveSpeed', 'in_moveSpeed')">Set</button></div></div>
        </div>
    </div>

    <!-- Section 2: Hero Primary Attributes -->
    <div class="card">
        <div class="card-header">
            <span>Hero Stats & Experience</span>
            <span id="heroAttrAddr" style="font-family: var(--font-mono);">0x00000000</span>
        </div>
        <div class="grid-4">
            <div class="stat-box"><span class="stat-label">Strength</span><div class="stat-input-group"><input type="number" id="in_str" placeholder="0"><button class="stat-btn" onclick="writeStat('strength', 'in_str')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Agility</span><div class="stat-input-group"><input type="number" id="in_agi" placeholder="0"><button class="stat-btn" onclick="writeStat('agility', 'in_agi')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Intelligence</span><div class="stat-input-group"><input type="number" id="in_int" placeholder="0"><button class="stat-btn" onclick="writeStat('intelligence', 'in_int')">Set</button></div></div>
            <div class="stat-box"><span class="stat-label">Experience</span><div class="stat-input-group"><input type="number" id="in_exp" placeholder="0"><button class="stat-btn" onclick="writeStat('experience', 'in_exp')">Set</button></div></div>
        </div>
    </div>

    <!-- Section 3: Dual Combat Attacks -->
    <div class="grid-2">
        <div class="card">
            <div class="card-header"><span>Attack 1 (Primary)</span></div>
            <div class="grid-3">
                <div class="stat-box"><span class="stat-label">Base 1</span><div class="stat-input-group"><input type="number" id="in_a1_b1"><button class="stat-btn" onclick="writeStat('a1_base1', 'in_a1_b1')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Base 2</span><div class="stat-input-group"><input type="number" id="in_a1_b2"><button class="stat-btn" onclick="writeStat('a1_base2', 'in_a1_b2')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Dice</span><div class="stat-input-group"><input type="number" id="in_a1_dice"><button class="stat-btn" onclick="writeStat('a1_dice', 'in_a1_dice')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Multiplier</span><div class="stat-input-group"><input type="number" id="in_a1_mult"><button class="stat-btn" onclick="writeStat('a1_mult', 'in_a1_mult')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Interval (s)</span><div class="stat-input-group"><input type="number" step="0.05" id="in_a1_int"><button class="stat-btn" onclick="writeStat('a1_interval', 'in_a1_int')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Range</span><div class="stat-input-group"><input type="number" id="in_a1_rng"><button class="stat-btn" onclick="writeStat('a1_range', 'in_a1_rng')">Set</button></div></div>
            </div>
        </div>
        <div class="card">
            <div class="card-header"><span>Attack 2 (Secondary)</span></div>
            <div class="grid-3">
                <div class="stat-box"><span class="stat-label">Base 1</span><div class="stat-input-group"><input type="number" id="in_a2_b1"><button class="stat-btn" onclick="writeStat('a2_base1', 'in_a2_b1')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Base 2</span><div class="stat-input-group"><input type="number" id="in_a2_b2"><button class="stat-btn" onclick="writeStat('a2_base2', 'in_a2_b2')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Dice</span><div class="stat-input-group"><input type="number" id="in_a2_dice"><button class="stat-btn" onclick="writeStat('a2_dice', 'in_a2_dice')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Multiplier</span><div class="stat-input-group"><input type="number" id="in_a2_mult"><button class="stat-btn" onclick="writeStat('a2_mult', 'in_a2_mult')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Interval (s)</span><div class="stat-input-group"><input type="number" step="0.05" id="in_a2_int"><button class="stat-btn" onclick="writeStat('a2_interval', 'in_a2_int')">Set</button></div></div>
                <div class="stat-box"><span class="stat-label">Range</span><div class="stat-input-group"><input type="number" id="in_a2_rng"><button class="stat-btn" onclick="writeStat('a2_range', 'in_a2_rng')">Set</button></div></div>
            </div>
        </div>
    </div>

    <!-- Section 4: 6-Slot Inventory -->
    <div class="card">
        <div class="card-header"><span>Hero Inventory (6 Slots)</span></div>
        <div class="grid-3" id="inventoryGrid"></div>
    </div>

    <div class="console-box" id="consoleOutput">
        <div class="log-entry"><span class="log-time">[SYS]</span>Controller Ready.</div>
    </div>
</div>
)rawliteral"
R"rawliteral(
<script>
    const API_BASE = window.location.origin + "/api";
    let isGodMode = false;
    let pollInterval = null;
    let resourcePollBusy = false;

    function log(msg, type = "") {
        const consoleEl = document.getElementById("consoleOutput");
        const time = new Date().toLocaleTimeString();
        const entry = document.createElement("div");
        entry.className = "log-entry";
        let colorClass = "";
        if (type === "success") colorClass = "log-success";
        if (type === "warn") colorClass = "log-warn";
        if (type === "error") colorClass = "log-error";
        entry.innerHTML = `<span class="log-time">[${time}]</span><span class="${colorClass}">${msg}</span>`;
        consoleEl.appendChild(entry);
        consoleEl.scrollTop = consoleEl.scrollHeight;
    }

    async function rehookGame() {
        log("Hooking war3.exe and resolving Game.dll...", "warn");
        try {
            const res = await fetch(`${API_BASE}/hook`, { method: "POST" });
            const data = await res.json();
            if (data.success) {
                document.getElementById("statusDot").classList.add("active");
                document.getElementById("statusText").innerText = `ATTACHED (PID: ${data.pid})`;
                log(`Successfully hooked process. PID: ${data.pid}, Game.dll: 0x${data.gameDll.toString(16).toUpperCase()}`, "success");
                startPolling();
                await fetchResources();
            } else {
                throw new Error(data.message || "Failed to locate module");
            }
        } catch (err) {
            document.getElementById("statusDot").classList.remove("active");
            document.getElementById("statusText").innerText = "DISCONNECTED";
            log(`Hook Error: ${err.message}`, "error");
        }
    }

    async function fetchSelectedUnit() {
        try {
            const res = await fetch(`${API_BASE}/unit`);
            const data = await res.json();
            if (data.found) {
                renderUnitData(data.unit);
            }
        } catch (err) {}
    }

    function renderUnitData(unit) {
        document.getElementById("targetAddr").innerText = `Unit: 0x${unit.address.toString(16).toUpperCase()}`;
        document.getElementById("heroAttrAddr").innerText = `HeroAttr: 0x${unit.heroAttributes.toString(16).toUpperCase()}`;

        const hpPercent = Math.min(100, Math.max(0, (unit.hp / unit.maxHp) * 100)) || 0;
        document.getElementById("hpFill").style.width = `${hpPercent}%`;
        document.getElementById("hpValues").innerText = `${unit.hp.toFixed(1)} / ${unit.maxHp.toFixed(1)}`;

        const mpPercent = Math.min(100, Math.max(0, (unit.mp / unit.maxMp) * 100)) || 0;
        document.getElementById("mpFill").style.width = `${mpPercent}%`;
        document.getElementById("mpValues").innerText = `${unit.mp.toFixed(1)} / ${unit.maxMp.toFixed(1)}`;

        const setVal = (id, val) => {
            const el = document.getElementById(id);
            if (el && document.activeElement !== el) {
                el.value = val;
            }
        };

        setVal("in_hp", unit.hp.toFixed(0));
        setVal("in_maxHp", unit.maxHp.toFixed(0));
        setVal("in_hpRegen", unit.hpRegen.toFixed(2));
        setVal("in_defense", unit.defense.toFixed(1));
        setVal("in_mp", unit.mp.toFixed(0));
        setVal("in_maxMp", unit.maxMp.toFixed(0));
        setVal("in_mpRegen", unit.mpRegen.toFixed(2));
        setVal("in_moveSpeed", unit.moveSpeed.toFixed(1));
        setVal("in_str", unit.strength);
        setVal("in_agi", unit.agility);
        setVal("in_int", unit.intelligence);
        setVal("in_exp", unit.experience);
        setVal("in_a1_b1", unit.a1_base1);
        setVal("in_a1_b2", unit.a1_base2);
        setVal("in_a1_dice", unit.a1_dice);
        setVal("in_a1_mult", unit.a1_multiplier);
        setVal("in_a1_int", unit.a1_interval.toFixed(2));
        setVal("in_a1_rng", unit.a1_range.toFixed(0));
        setVal("in_a2_b1", unit.a2_base1);
        setVal("in_a2_b2", unit.a2_base2);
        setVal("in_a2_dice", unit.a2_dice);
        setVal("in_a2_mult", unit.a2_multiplier);
        setVal("in_a2_int", unit.a2_interval.toFixed(2));
        setVal("in_a2_rng", unit.a2_range.toFixed(0));

        const invGrid = document.getElementById("inventoryGrid");
        if (invGrid.children.length !== 6) {
            invGrid.innerHTML = "";
            for (let i = 0; i < 6; ++i) {
                const slotBox = document.createElement("div");
                slotBox.className = "item-slot";
                slotBox.id = `inv_slot_${i}`;
                invGrid.appendChild(slotBox);
            }
        }

        for (let i = 0; i < 6; ++i) {
            const item = unit.items && unit.items[i] ? unit.items[i] : null;
            const slotBox = document.getElementById(`inv_slot_${i}`);
            if (item) {
                const headerHtml = `<span>Slot ${i+1}: 0x${item.address.toString(16).toUpperCase()}</span><span>Raw: 0x${item.rawName.toString(16).toUpperCase()}</span>`;
                let header = slotBox.querySelector(".item-slot-header");
                if (!header) {
                    slotBox.innerHTML = `
                        <div class="item-slot-header">${headerHtml}</div>
                        <div class="stat-input-group">
                            <input type="number" id="item_charge_${i}" placeholder="Charges">
                            <button class="stat-btn" onclick="writeItemCharges(${i}, 'item_charge_${i}')">Set</button>
                        </div>
                    `;
                } else {
                    header.innerHTML = headerHtml;
                }
                const input = document.getElementById(`item_charge_${i}`);
                if (input && document.activeElement !== input) {
                    input.value = item.charges;
                }
            } else {
                slotBox.innerHTML = `<div class="item-slot-header"><span>Slot ${i+1}</span><span>[Empty]</span></div>`;
            }
        }
    }
)rawliteral"
R"rawliteral(
    function renderResources(resources) {
        const grid = document.getElementById("resourcesGrid");

        if (grid.children.length !== resources.length) {
            grid.innerHTML = "";
            resources.forEach((res) => {
                const card = document.createElement("div");
                card.className = "resource-card";
                card.id = `res_card_${res.player}`;
                card.innerHTML = `
                    <div class="resource-header">
                        <span>PLAYER ${res.player}</span>
                        <span id="res_addr_${res.player}">0x0</span>
                    </div>
                    <div class="resource-values">
                        <div class="resource-value gold">
                            <div class="label">Gold</div>
                            <div class="value" id="val_gold_${res.player}">0</div>
                        </div>
                        <div class="resource-value lumber">
                            <div class="label">Lumber</div>
                            <div class="value" id="val_lumber_${res.player}">0</div>
                        </div>
                    </div>
                    <div class="resource-edit">
                        <input type="number" min="0" step="1" id="gold_${res.player}" placeholder="Set Gold">
                        <button class="stat-btn" onclick="writeResource(${res.player}, 'gold', 'gold_${res.player}')">Set Gold</button>
                    </div>
                    <div class="resource-edit">
                        <input type="number" min="0" step="1" id="lumber_${res.player}" placeholder="Set Lumber">
                        <button class="stat-btn" onclick="writeResource(${res.player}, 'lumber', 'lumber_${res.player}')">Set Lumber</button>
                    </div>
                `;
                grid.appendChild(card);
            });
        }

        let resolved = 0;
        resources.forEach((res) => {
            resolved++;
            const addrEl = document.getElementById(`res_addr_${res.player}`);
            const goldEl = document.getElementById(`val_gold_${res.player}`);
            const lumberEl = document.getElementById(`val_lumber_${res.player}`);

            if (addrEl) addrEl.innerText = `0x${Number(res.goldAddress || 0).toString(16).toUpperCase()}`;
            if (goldEl) goldEl.innerText = Number(res.gold || 0).toLocaleString();
            if (lumberEl) lumberEl.innerText = Number(res.lumber || 0).toLocaleString();
        });

        document.getElementById("resourceSummary").innerText = `${resolved} / 12 players resolved`;
    }

    async function fetchResources() {
        if (resourcePollBusy) return;
        resourcePollBusy = true;
        try {
            const res = await fetch(`${API_BASE}/resources`);
            const data = await res.json();
            if (data.success) {
                renderResources(data.resources || []);
            } else {
                document.getElementById("resourceSummary").innerText = "Not attached";
            }
        } catch (err) {
            document.getElementById("resourceSummary").innerText = "API error";
        } finally {
            resourcePollBusy = false;
        }
    }

    async function writeResource(player, type, inputId) {
        const input = document.getElementById(inputId);
        const val = parseInt(input.value, 10);
        if (!Number.isFinite(val) || val < 0) return;

        try {
            const res = await fetch(`${API_BASE}/write`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ field: type, value: val, player: player })
            });
            const data = await res.json();
            if (data.success) {
                log(`Player ${player} ${type} -> ${val}`, "success");
                input.value = "";
                await fetchResources();
            } else {
                log(`Player ${player} ${type} write failed.`, "error");
            }
        } catch (err) {
            log(`Resource write error: ${err.message}`, "error");
        }
    }

    async function writeStat(field, inputId) {
        const val = parseFloat(document.getElementById(inputId).value);
        if (isNaN(val)) return;
        try {
            const res = await fetch(`${API_BASE}/write`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ field: field, value: val })
            });
            const data = await res.json();
            if (data.success) {
                log(`Updated ${field} -> ${val}`, "success");
                fetchSelectedUnit();
            } else {
                log(`Write to ${field} failed.`, "error");
            }
        } catch (err) {
            log(`Write error: ${err.message}`, "error");
        }
    }

    async function writeItemCharges(slot, inputId) {
        const val = parseInt(document.getElementById(inputId).value, 10);
        if (isNaN(val)) return;
        try {
            const res = await fetch(`${API_BASE}/write_item`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ slot: slot, charges: val })
            });
            const data = await res.json();
            if (data.success) {
                log(`Slot ${slot+1} charges set to ${val}`, "success");
                fetchSelectedUnit();
            }
        } catch (err) {}
    }

    function toggleGodMode() {
        isGodMode = !isGodMode;
        const btn = document.getElementById("godModeBtn");
        if (isGodMode) {
            btn.innerText = "God Mode: ACTIVE";
            btn.style.background = "var(--accent-primary-dim)";
            btn.style.borderColor = "var(--accent-primary)";
            btn.style.color = "var(--accent-primary)";
            log("God Mode ON (Continuous HP/MP lock)", "success");
        } else {
            btn.innerText = "God Mode: OFF";
            btn.style.background = "rgba(255, 51, 85, 0.12)";
            btn.style.borderColor = "var(--accent-danger)";
            btn.style.color = "var(--accent-danger)";
            log("God Mode OFF.", "warn");
        }
    }

    function startPolling() {
        if (pollInterval) clearInterval(pollInterval);
        pollInterval = setInterval(async () => {
            await fetchSelectedUnit();
            await fetchResources();
            if (isGodMode) {
                fetch(`${API_BASE}/write`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ field: "hp", value: 999999.0 })
                });
                fetch(`${API_BASE}/write`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ field: "mp", value: 999999.0 })
                });
            }
        }, 400);
    }

    window.onload = () => {
        rehookGame();
    };
</script>
</body>
</html>
)rawliteral";

#endif
