import { useState } from "react";

const PHASES = [
  {
    id: 1, label: "Phase 1", title: "Systemarchitektur", done: true,
    blocks: [{ id: "1.1", title: "Systemübersicht", done: true, parts: [] }]
  },
  {
    id: 2, label: "Phase 2", title: "Schaltungskonzept", done: false,
    blocks: [
      {
        id: "2.1", title: "Stromversorgung Außeneinheit", done: true,
        parts: [
          { ref: "U1",    desc: "LMR33630CDDAR",       pkg: "8SOPWR",   note: "Step-Down 12V→3,3V" },
          { ref: "L1",    desc: "74438356018HT",        pkg: "5×5mm",    note: "1,8µH Würth" },
          { ref: "Q1",    desc: "DMG2305UX-7",          pkg: "SOT-23",   note: "Verpolschutz" },
          { ref: "D1",    desc: "SMBJ15A-E3/52",        pkg: "DO-214AA", note: "TVS 15V" },
          { ref: "Rfbt",  desc: "100kΩ Yageo",          pkg: "0603",     note: "Spannungsteiler" },
          { ref: "Rfbb",  desc: "43,2kΩ Yageo",         pkg: "0603",     note: "Spannungsteiler" },
          { ref: "Rpg",   desc: "100kΩ Yageo",          pkg: "0603",     note: "Power-Good" },
          { ref: "Cin",   desc: "10µF 25V X7R",         pkg: "0805",     note: "Eingang Bulk" },
          { ref: "Cinx",  desc: "220nF 25V X7R",        pkg: "0603",     note: "Eingang HF" },
          { ref: "Cout",  desc: "2× 22µF 10V X7R",      pkg: "0805",     note: "Ausgang Bulk" },
          { ref: "Cboot", desc: "100nF 16V X7R",        pkg: "0603",     note: "Bootstrap" },
          { ref: "Cvcc",  desc: "1µF 25V X7R",          pkg: "0603",     note: "VCC Bypass" },
        ]
      },
      { id: "2.2", title: "ESP32 Beschaltung", done: true,
        parts: [
          { ref: "U2",      desc: "ESP32-WROOM-32E",        pkg: "SMD 38-Pin", note: "WiFi+BT, CE-zertifiziert" },
          { ref: "C_bulk",  desc: "100µF 6,3V Elektrolyt",  pkg: "Ø6,3mm",    note: "Bulk-Puffer WiFi-Peaks" },
          { ref: "R_EN",    desc: "10kΩ Yageo",             pkg: "0603",       note: "Pull-up Enable" },
          { ref: "S_RST",   desc: "PTS526 C&K",             pkg: "5,2×5,2mm",  note: "Reset Taster" },
          { ref: "R_BOOT",  desc: "10kΩ Yageo",             pkg: "0603",       note: "Pull-up GPIO0" },
          { ref: "R_GPIO2", desc: "10kΩ Yageo",             pkg: "0603",       note: "Pull-down GPIO2" },
          { ref: "S_BOOT",  desc: "PTS526 C&K",             pkg: "5,2×5,2mm",  note: "Boot Taster" },
          { ref: "J_UART",  desc: "4-Pin Header 90°",       pkg: "THT",        note: "UART Programmierung" },
          { ref: "R_RXD",   desc: "100Ω Yageo",             pkg: "0603",       note: "Schutz RXD0" },
        ]
      },
      { id: "2.3", title: "Kapazitive Touch-Taster", done: true,
        parts: [
          { ref: "R_TOUCH1", desc: "1kΩ Yageo",         pkg: "0603", note: "ESD-Schutz Touch EG" },
          { ref: "R_TOUCH2", desc: "1kΩ Yageo",         pkg: "0603", note: "ESD-Schutz Touch OG" },
          { ref: "J_TOUCH1", desc: "JST-PH 2-Pin 2,0mm",pkg: "SMD",  note: "Anschluss Bronzetaste EG" },
          { ref: "J_TOUCH2", desc: "JST-PH 2-Pin 2,0mm",pkg: "SMD",  note: "Anschluss Bronzetaste OG" },
        ]
      },
      { id: "2.4", title: "SD + Class-D + Lautsprecher", done: true,
        parts: [
          { ref: "J_SD",    desc: "Molex 1040310811",      pkg: "SMD",      note: "MicroSD Push-Push" },
          { ref: "R_SD1-4", desc: "33Ω Yageo",             pkg: "0603",     note: "SPI Dämpfung" },
          { ref: "U3",      desc: "MAX98357AETE+T",         pkg: "TSSOP-16", note: "Class-D I2S Verstärker" },
          { ref: "LS1",     desc: "Visaton K 36 WP",        pkg: "36mm",     note: "8Ω 1W Weatherproof" },
          { ref: "J_SPK",   desc: "JST-PH 2-Pin 2,0mm",    pkg: "SMD",      note: "Lautsprecher Anschluss" },
        ]
      },
      { id: "2.5", title: "Türöffner MOSFET", done: true,
        parts: [
          { ref: "Q2",     desc: "IRLML6344TRPBF",      pkg: "SOT-23",   note: "N-Kanal MOSFET, DNP" },
          { ref: "D2",     desc: "SS14-E3/61T",          pkg: "DO-214AC", note: "Freilaufdiode, DNP" },
          { ref: "R_GATE", desc: "10kΩ Yageo",           pkg: "0603",     note: "Gate Vorwiderstand" },
          { ref: "R_GND",  desc: "10kΩ Yageo",           pkg: "0603",     note: "Gate Pull-down" },
          { ref: "J_DOOR", desc: "WAGO 2086-1202",       pkg: "SMD",      note: "Push-in Klemme, DNP" },
        ]
      },
      { id: "2.6", title: "Steckverbinder / Klemmen", done: true,
        parts: [
          { ref: "J_PWR",  desc: "WAGO 2086-1202", pkg: "SMD", note: "12V Eingang Push-in" },
          { ref: "J_DOOR", desc: "WAGO 2086-1202", pkg: "SMD", note: "Türöffner Push-in, DNP" },
        ]
      },
      { id: "2.7", title: "Stromversorgung Inneneinheit",done: false, parts: [] },
      { id: "2.8", title: "Inneneinheit Blöcke",         done: false, parts: [] },
    ]
  },
  {
    id: 3, label: "Phase 3", title: "KiCad Schaltplan", done: false,
    blocks: [{ id: "3.1", title: "Schaltplan Außeneinheit", done: false, parts: [] },
             { id: "3.2", title: "Schaltplan Inneneinheit", done: false, parts: [] }]
  },
  {
    id: 4, label: "Phase 4", title: "PCB Layout", done: false,
    blocks: [{ id: "4.1", title: "Layout Außeneinheit", done: false, parts: [] },
             { id: "4.2", title: "Layout Inneneinheit", done: false, parts: [] }]
  },
  {
    id: 5, label: "Phase 5", title: "Fertigung & Bestückung", done: false,
    blocks: [{ id: "5.1", title: "Gerber-Export & Bestellung", done: false, parts: [] },
             { id: "5.2", title: "Bestückung & Inbetriebnahme", done: false, parts: [] }]
  },
  {
    id: 6, label: "Phase 6", title: "Firmware & HA", done: false,
    blocks: [{ id: "6.1", title: "ESPHome Konfiguration", done: false, parts: [] },
             { id: "6.2", title: "Home Assistant Integration", done: false, parts: [] }]
  },
];

const BLOCK_DIAGRAM = {
  außen: {
    label: "Außeneinheit",
    color: "#e8a87c",
    blocks: [
      { id: "pwr",   label: "Stromversorgung", sub: "12V → 3,3V LMR33630", done: true },
      { id: "mcu",   label: "ESP32",           sub: "WiFi + ESP-NOW",      done: true },
      { id: "touch", label: "Touch-Taster",    sub: "2× kapazitiv",        done: true },
      { id: "audio", label: "Audio",           sub: "SD + Class-D",        done: true },
      { id: "relay", label: "Türöffner",      sub: "MOSFET (DNP)",        done: true },
      { id: "conn",  label: "Steckverbinder",  sub: "12V + Türöffner",     done: true },
    ]
  },
  innen: {
    label: "Inneneinheit (2×)",
    color: "#4af7a0",
    blocks: [
      { id: "pwr_i",   label: "Stromversorgung", sub: "USB-C 5V → 3,3V",  done: false },
      { id: "mcu_i",   label: "ESP32",           sub: "WiFi + ESP-NOW",    done: false },
      { id: "touch_i", label: "Touch-Taster",    sub: "1× kapazitiv",      done: false },
      { id: "audio_i", label: "Audio",           sub: "SD + Class-D",      done: false },
    ]
  }
};

export default function Dashboard() {
  const [activePhase, setActivePhase] = useState(2);
  const [activeBlock, setActiveBlock] = useState("2.1");

  const phase = PHASES.find(p => p.id === activePhase);
  const block = phase?.blocks.find(b => b.id === activeBlock);

  const totalBlocks = PHASES.flatMap(p => p.blocks).length;
  const doneBlocks  = PHASES.flatMap(p => p.blocks).filter(b => b.done).length;
  const pct = Math.round((doneBlocks / totalBlocks) * 100);

  return (
    <div style={{ background: "#0a0f1a", minHeight: "100vh", fontFamily: "'Courier New', monospace", color: "#e0e8ff", padding: "1.5rem" }}>

      {/* Header */}
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-end", marginBottom: "1.5rem", flexWrap: "wrap", gap: "1rem" }}>
        <div>
          <div style={{ fontSize: "1.8rem", fontFamily: "Georgia, serif", letterSpacing: "0.3em", color: "#7eb8f7" }}>ClearBell</div>
          <div style={{ fontSize: "0.65rem", color: "#4a6080", letterSpacing: "0.2em", marginTop: "0.2rem" }}>PROJEKT-DASHBOARD · REV 2</div>
        </div>
        <div style={{ textAlign: "right" }}>
          <div style={{ fontSize: "0.65rem", color: "#4a6080", letterSpacing: "0.1em", marginBottom: "0.4rem" }}>FORTSCHRITT {pct}%</div>
          <div style={{ width: "200px", height: "6px", background: "#0d1520", borderRadius: "3px", overflow: "hidden" }}>
            <div style={{ width: `${pct}%`, height: "100%", background: "#7eb8f7", borderRadius: "3px", transition: "width 0.5s" }} />
          </div>
          <div style={{ fontSize: "0.6rem", color: "#4a6080", marginTop: "0.3rem" }}>{doneBlocks} / {totalBlocks} Blöcke abgeschlossen</div>
        </div>
      </div>

      <div style={{ display: "grid", gridTemplateColumns: "220px 1fr", gap: "1rem", marginBottom: "1rem" }}>

        {/* Phase Navigation */}
        <div style={{ display: "flex", flexDirection: "column", gap: "0.4rem" }}>
          <div style={{ fontSize: "0.6rem", color: "#4a6080", letterSpacing: "0.15em", marginBottom: "0.3rem" }}>PHASEN</div>
          {PHASES.map(p => (
            <button key={p.id} onClick={() => { setActivePhase(p.id); setActiveBlock(p.blocks[0].id); }}
              style={{
                background: activePhase === p.id ? "#0d1520" : "transparent",
                border: `1px solid ${activePhase === p.id ? "#7eb8f7" : "#1a2535"}`,
                borderLeft: `3px solid ${p.done ? "#4af7a0" : activePhase === p.id ? "#7eb8f7" : "#2a3545"}`,
                borderRadius: "3px", padding: "0.5rem 0.7rem", cursor: "pointer",
                textAlign: "left", transition: "all 0.2s",
              }}>
              <div style={{ fontSize: "0.6rem", color: p.done ? "#4af7a0" : "#4a6080", letterSpacing: "0.1em" }}>{p.label} {p.done ? "✓" : ""}</div>
              <div style={{ fontSize: "0.7rem", color: activePhase === p.id ? "#e0e8ff" : "#6a7890", marginTop: "0.1rem" }}>{p.title}</div>
            </button>
          ))}
        </div>

        {/* Block Navigation + Detail */}
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1rem" }}>

          {/* Blocks of active phase */}
          <div>
            <div style={{ fontSize: "0.6rem", color: "#4a6080", letterSpacing: "0.15em", marginBottom: "0.5rem" }}>BLÖCKE — {phase?.title}</div>
            <div style={{ display: "flex", flexDirection: "column", gap: "0.3rem" }}>
              {phase?.blocks.map(b => (
                <button key={b.id} onClick={() => setActiveBlock(b.id)}
                  style={{
                    background: activeBlock === b.id ? "#0f1825" : "#0d1520",
                    border: `1px solid ${activeBlock === b.id ? "#e8a87c44" : "#1a2535"}`,
                    borderLeft: `2px solid ${b.done ? "#4af7a0" : activeBlock === b.id ? "#e8a87c" : "#2a3545"}`,
                    borderRadius: "3px", padding: "0.5rem 0.7rem", cursor: "pointer",
                    textAlign: "left", transition: "all 0.2s",
                    boxShadow: activeBlock === b.id ? "0 0 10px #e8a87c11" : "none",
                  }}>
                  <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                    <span style={{ fontSize: "0.68rem", color: activeBlock === b.id ? "#e0e8ff" : "#6a7890" }}>{b.title}</span>
                    <span style={{ fontSize: "0.6rem", color: b.done ? "#4af7a0" : "#2a3545" }}>{b.done ? "✓" : "○"}</span>
                  </div>
                  {b.done && b.parts.length > 0 && (
                    <div style={{ fontSize: "0.55rem", color: "#4a6080", marginTop: "0.2rem" }}>{b.parts.length} Bauteile</div>
                  )}
                </button>
              ))}
            </div>
          </div>

          {/* Block Detail */}
          <div>
            <div style={{ fontSize: "0.6rem", color: "#4a6080", letterSpacing: "0.15em", marginBottom: "0.5rem" }}>DETAIL — {block?.title}</div>
            <div style={{ background: "#0d1520", border: "1px solid #1a2535", borderRadius: "4px", padding: "0.8rem", minHeight: "200px" }}>
              {block?.done && block.parts.length > 0 ? (
                <div>
                  <div style={{ fontSize: "0.6rem", color: "#4af7a0", letterSpacing: "0.1em", marginBottom: "0.5rem" }}>✓ ABGESCHLOSSEN — BOM</div>
                  {block.parts.map((p, i) => (
                    <div key={i} style={{
                      display: "grid", gridTemplateColumns: "40px 1fr 45px",
                      borderBottom: "1px solid #1a2535", padding: "0.25rem 0", gap: "0.5rem",
                    }}>
                      <span style={{ fontSize: "0.6rem", color: "#e8a87c" }}>{p.ref}</span>
                      <span style={{ fontSize: "0.6rem", color: "#8090a8" }}>{p.desc}</span>
                      <span style={{ fontSize: "0.55rem", color: "#4a6080", textAlign: "right" }}>{p.pkg}</span>
                    </div>
                  ))}
                </div>
              ) : block?.done ? (
                <div style={{ fontSize: "0.65rem", color: "#4af7a0" }}>✓ Abgeschlossen</div>
              ) : (
                <div style={{ fontSize: "0.65rem", color: "#2a3545", marginTop: "2rem", textAlign: "center" }}>
                  ○ Noch offen
                </div>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Block Diagram */}
      <div style={{ marginTop: "1rem" }}>
        <div style={{ fontSize: "0.6rem", color: "#4a6080", letterSpacing: "0.15em", marginBottom: "0.8rem" }}>BLOCKSCHALTBILD — WÄCHST MIT DEM FORTSCHRITT</div>
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1rem" }}>
          {Object.entries(BLOCK_DIAGRAM).map(([key, unit]) => (
            <div key={key} style={{ background: "#0d1520", border: `1px solid ${unit.color}33`, borderTop: `2px solid ${unit.color}`, borderRadius: "4px", padding: "0.8rem" }}>
              <div style={{ fontSize: "0.7rem", color: unit.color, letterSpacing: "0.1em", marginBottom: "0.6rem", fontWeight: "bold" }}>{unit.label}</div>
              <div style={{ display: "flex", flexDirection: "column", gap: "0.3rem" }}>
                {unit.blocks.map((b, i) => (
                  <div key={i} style={{
                    display: "flex", justifyContent: "space-between", alignItems: "center",
                    background: b.done ? "#0f1825" : "#080d14",
                    border: `1px solid ${b.done ? unit.color + "44" : "#1a2535"}`,
                    borderLeft: `2px solid ${b.done ? unit.color : "#2a3545"}`,
                    borderRadius: "3px", padding: "0.4rem 0.6rem",
                    opacity: b.done ? 1 : 0.5,
                  }}>
                    <div>
                      <div style={{ fontSize: "0.65rem", color: b.done ? "#e0e8ff" : "#4a6080" }}>{b.label}</div>
                      <div style={{ fontSize: "0.55rem", color: b.done ? unit.color : "#2a3545", marginTop: "0.1rem" }}>{b.sub}</div>
                    </div>
                    <span style={{ fontSize: "0.65rem", color: b.done ? unit.color : "#2a3545" }}>{b.done ? "✓" : "○"}</span>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Footer */}
      <div style={{ marginTop: "1.2rem", fontSize: "0.55rem", color: "#1a2535", letterSpacing: "0.15em", textAlign: "center" }}>
        CLEARBELL · AUSSENEINHEIT ✓ KOMPLETT · NÄCHSTER SCHRITT: BLOCK 2.7 INNENEINHEIT STROMVERSORGUNG
      </div>
    </div>
  );
}
