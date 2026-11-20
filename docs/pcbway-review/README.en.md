# Review: PCB fabrication and assembly by PCBWay (ClearBell V0.2)

🇩🇪 [Deutsche Fassung](README.md)

> **Disclosure:** [PCBWay](https://www.pcbway.com) sponsored the fabrication of the ClearBell V0.2 PCBs free of
> charge. I paid for assembly and components myself at the regular price. This review is part of that agreement.
> The content and the verdict are my own; PCBWay had no influence on it beforehand.

I work as a PCB design engineer and handle boards from various manufacturers almost every day. This review is based on
a visual inspection of both board types, assembled and bare, and on the complete bring-up followed by a 24-hour
endurance test.

**Summary:** The quality of both the PCBs and the assembly is very good. I found no defect on the boards or in the
assembly. Every issue that came up during bring-up traces back to my own design or to operator error — none to
manufacturing.

---

## The order

| | Outdoor unit | Indoor unit |
|---|---|---|
| Size | 110 × 60 mm | 98 × 60 mm |
| Stack-up | 4 layers, 1.6 mm, FR-4 (S1000H, TG150), 1 oz outer and inner | same |
| Finish | ENIG, green solder mask, white silkscreen (top only) | same |
| Holes | 432 plated, 7 non-plated | 379 plated, 12 non-plated |
| Assembly | single-sided, 56 parts | single-sided, 33 parts |
| Critical parts | ESP32-S3 module, LMR33630 (buck), MAX98357A (audio amp, QFN), MEMS microphone, service USB-C | ESP32-S3 module, TLV62568 (buck), MAX98357A (QFN), MEMS microphone, USB-C |

The smallest clearance in the layout is 0.2 mm (at the ESP32-S3 module), right at PCBWay's manufacturing limit.
Schematics, layouts and manufacturing data are in [`hardware/`](../../hardware/).

**Timeline:** manufacturing data uploaded on 18 Sep 2026, questions on individual parts on 19 Sep, fabrication and
assembly confirmed on 26 Sep, delivered on 24 Oct 2026.

---

## Ordering and communication

PCBWay reviewed the bill of materials before assembly and asked targeted questions about four line items (part numbers
and permitted substitutes). The questions were technically justified and came as an Excel sheet that was quick to
answer. My BOM includes a "substitute allowed" column; PCBWay respected it, and nothing was swapped on parts marked
"no substitute".

I was very satisfied with the communication. The website is good as well: it shows in detail which production step
the order is currently in, so you can follow the progress yourself at any time.

For the MEMS microphone I had supplied special handling notes (no ultrasonic or wash cleaning, no compressed air,
no-clean paste). The microphone works on every board tested — the notes were clearly followed.

---

## Bare PCB

| Indoor, top | Indoor, ENIG detail | Indoor, bottom |
|---|---|---|
| ![Indoor unit bare, top side](img/01_innen_unbestueckt_oben.jpg) | ![Indoor unit bare, ESP32 module pads and microphone port](img/02_innen_unbestueckt_enig_detail.jpg) | ![Indoor unit bare, bottom side](img/03_innen_unbestueckt_unten.jpg) |

| Outdoor, top | Outdoor, power section | Outdoor, bottom |
|---|---|---|
| ![Outdoor unit bare, top side](img/09_aussen_unbestueckt_oben.jpg) | ![Outdoor unit bare, power section with regulator thermal pad](img/10_aussen_unbestueckt_versorgung.jpg) | ![Outdoor unit bare, bottom side](img/11_aussen_unbestueckt_unten.jpg) |

- **Solder mask:** even coverage, no bubbles, scratches or voids. The pad openings are accurately registered, including
  the fine pad row of the ESP32 module and the QFN of the audio amplifier.
- **Silkscreen:** sharp and fully legible down to the smallest reference designators and test point names. No ink on
  pads.
- **ENIG finish:** smooth and flat, including the large thermal pads (regulator, ESP32 module with via array). No
  discoloration.
- **Drilling and outline:** holes clean and centred in their annular rings; the microphone sound port has an intact
  copper ring. The routed outline, including the cut-out for the module antenna and the rounded corners, is accurate
  and burr-free.
- **Material:** no delamination, no blistering, no visible warpage. Both boards lie flat.

## Assembly

| Indoor, assembled | Indoor, ESP32-S3 module | Indoor, bottom |
|---|---|---|
| ![Indoor unit assembled, top side](img/04_innen_bestueckt_oben.jpg) | ![Indoor unit, ESP32-S3-WROOM-1](img/05_innen_esp32_modul.jpg) | ![Indoor unit assembled, bottom side](img/08_innen_bestueckt_unten.jpg) |

| Outdoor, assembled | Outdoor, ESP32-S3 module | Outdoor, bottom |
|---|---|---|
| ![Outdoor unit assembled, top side](img/12_aussen_bestueckt_oben.jpg) | ![Outdoor unit, ESP32-S3-WROOM-1](img/16_aussen_esp32_modul.jpg) | ![Outdoor unit assembled, bottom side](img/17_aussen_bestueckt_unten.jpg) |

**Critical areas in detail:**

| USB-C and regulator (indoor) | Audio amplifier QFN (indoor) |
|---|---|
| ![Indoor unit, USB-C connector, ESD diode and buck regulator](img/06_innen_usb_c_regler.jpg) | ![Indoor unit, MAX98357A in QFN package](img/07_innen_audioverstaerker_qfn.jpg) |

| 12 V → 3.3 V supply (outdoor) | Service USB-C (outdoor) | Audio amplifier QFN (outdoor) |
|---|---|---|
| ![Outdoor unit, power section with LMR33630](img/13_aussen_versorgung.jpg) | ![Outdoor unit, service USB-C](img/14_aussen_usb_c_service.jpg) | ![Outdoor unit, MAX98357A in QFN package](img/15_aussen_audioverstaerker_qfn.jpg) |

- **Placement:** every part sits straight and centred on its pads, with no offset or rotation. Polarity and orientation
  are correct on all polarised parts (diodes, electrolytic capacitor, ICs, LED).
- **USB-C:** the 0.5 mm pitch pads of both connectors are cleanly soldered without bridges, the mounting tabs fully
  wetted. Both connectors sit firmly and flush with the board edge.
- **Audio amplifier (QFN):** even solder volume on all terminals, no bridges, package seated flat.
- **ESP32-S3 module:** all castellated pads evenly wetted, module flat and correctly oriented, antenna correctly placed
  over the cut-out.
- **Solder joints in general:** uniform and shiny with a clean fillet. No solder balls, no tombstoning, no
  objectionable flux residue.
- **BOM:** all parts fitted, complete and in the correct variant, including those with substitutes permitted per my
  instructions.

---

## Function: bring-up and endurance test

Bring-up proceeded stage by stage from 24 Oct 2026, first on one board of each type (IN-1, AU-1):

| Stage | Result |
|---|---|
| Visual inspection and resistance checks | all check points OK (indoor 14, outdoor 21), no shorts |
| Power supply | +3.3 V: indoor 3.293 V, outdoor 3.323 V; reverse-polarity protection works |
| Controller, USB | ESP32-S3 with 16 MB flash and 8 MB PSRAM detected, flashing via native USB first time |
| Audio | clean tone, no pop on switch-on or switch-off |
| Microphone | works, speech recordings clearly intelligible |
| Touch | stable idle readings, clear touch detection |
| Wi-Fi with audio at full volume | 300 of 300 packets, no reset |
| 24-hour endurance test, three boards | passed: no reset, no crash, 0 failed doorbell rings |

All three boards now run the new V0.2 firmware. **Not a single finding from bring-up could be traced to fabrication or
assembly** — the issues found were my own design decisions (e.g. a small reverse current through a decoupling diode) or
operator errors while flashing. They are documented openly in the repository and will be addressed in V0.3.

---

## Verdict

| Criterion | Rating |
|---|---|
| PCB quality | very good |
| Assembly quality | very good |
| Critical areas (USB-C, QFN, module) | very good, no bridges |
| Communication and engineering questions | very good, technically justified and quickly resolved |
| Website and order tracking | very good, every production step visible |
| Function on delivery | all three boards brought up worked first time |

For a 4-layer design with a module, QFN, USB-C and a MEMS microphone, this is the result I would expect in a
professional environment as well. I would use PCBWay for fabrication and assembly again.

**Limits of this review:** I inspected with the naked eye, a phone camera and the measurements taken during bring-up.
No X-ray of hidden joints (thermal pads under the module, regulator and QFN), no cross-section. Their quality is
confirmed indirectly by function: bring-up and the 24-hour endurance test without any anomaly. The reddish tint in some
photos comes from the lighting; the solder mask is green.

---

*André Körner, November 2026 — project [ClearBell](../../README.md)*
