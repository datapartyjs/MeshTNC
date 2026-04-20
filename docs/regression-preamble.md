# Preamble Migration Regression Test Plan

Verifies that the `jgromes/RadioLib` → `SourceParts/Preamble` migration produces no regressions across all MeshTNC variants. Preamble diverges from RadioLib at commit `802b969f0` with two hardware fixes and a new versioning scheme.

---

## Preamble-specific changes under test

| # | Area | Change | Risk |
|---|------|--------|------|
| 1 | SX128x `setBandwidth()` | Snaps to nearest valid BW instead of returning error on approximate input | Low — only widens accepted values |
| 2 | SX127x `reset()` when NRESET=NC | Adds 5 ms crystal startup delay before first SPI transaction | Medium — timing-sensitive cold-boot path |

---

## 1. Build matrix

All 38 variants must compile with zero errors.

```bash
cd /run/media/bleepbloop/Blue_Popcorn/Consulting/mesh-hardware/Software/MeshTNC
pio run
```

**Pass criteria:** `N succeeded, 0 failed` for every environment.

Priority order if partial hardware is available:

| Priority | Chip | Variants |
|----------|------|----------|
| 1 | SX1262 | rak4631, lilygo_tbeam_supreme_SX1262, heltec_v3, xiao_s3_wio, … (25 total) |
| 2 | SX1276 | lilygo_tbeam_SX1276, lilygo_t3s3_sx1276, meshtnc_ultra (SX1276 path) |
| 3 | SX1281 | meshtnc_ultra (2.4 GHz path) |
| 4 | SX1268 | lilygo_tlora_v2_1, generic-e22, … |
| 5 | LR1110 | lilygo_tbeam_supreme_SX1262 variants |
| 6 | STM32WLx | wio-e5-dev, wio-e5-mini |

---

## 2. API surface — no RadioLib.h references

```bash
grep -r "RadioLib.h" src/ variants/ examples/
```

**Pass criteria:** zero matches. (References inside `lib/Preamble/` itself are expected and ignored.)

---

## 3. Preamble fix #1 — SX128x bandwidth snapping

**Applies to:** meshtnc_ultra (SX1281).

**What changed:** `SX128x::setBandwidth()` in Preamble accepts approximate values and snaps to the nearest of `{203.125, 406.25, 812.5, 1625.0}` kHz. RadioLib returned `RADIOLIB_ERR_INVALID_BANDWIDTH` on non-exact input.

### 3a. Exact-value call (current usage — must still work)

In `target.cpp`, `radio_sx1281.std_init(2400.0, 203.125f, ...)` passes the exact value. Confirm it initialises successfully.

**Serial output expected:**
```
SX1281 ready
```

### 3b. Approximate-value call (regression against RadioLib behaviour)

Temporarily change `203.125f` → `203.0f` in `target.cpp`, flash, confirm init still succeeds.

**Pass criteria:** `SX1281 ready` with no `RADIOLIB_ERR_INVALID_BANDWIDTH` error code.

**Restore** exact value after test.

---

## 4. Preamble fix #2 — SX127x crystal startup delay (NRESET=NC)

**Applies to:** all SX1276 and SX1272 variants where the reset pin is not wired (`RADIOLIB_NC`). On meshtnc_ultra both radio NRESETs are tied to CHIP_PU — not a GPIO — so RADIOLIB_NC is correct for both.

**What changed:** `SX1272::reset()` and `SX1278::reset()` add `hal->delay(5)` when NRESET=NC, giving the crystal time to start before the first SPI transaction.

### 4a. Cold boot — SX1276 init must succeed on first attempt

Power-cycle the board (do not reset via button — that keeps the crystal warm). Watch serial.

**Pass criteria:**
```
SX1276 ready
```
on the first boot attempt. No `WARN: SX1276 915MHz init failed` on cold power-on.

### 4b. Warm reset — must still succeed

Reset via button or `EN` pin (crystal already oscillating). Confirm init still succeeds.

**Pass criteria:** same as 4a.

### 4c. No GPIO pre-transfer timeout (Preamble + setRadio fix combined)

After full init, monitor serial for 60 seconds.

**Pass criteria:** zero `GPIO pre-transfer timeout` lines in serial output.

---

## 5. Radio functional tests

Run on physical hardware for each chip family. Use two boards (one TX, one RX) on the same frequency/SF/BW/CR.

### 5a. Transmit

Trigger a mesh advertisement or CLI send command. Confirm TX completes.

**Pass criteria:**
- `startSendRaw()` returns `true`
- `isSendComplete()` returns `true` within expected airtime
- No `startTransmit` error codes in serial

### 5b. Receive

**Pass criteria:**
- Packet received from known transmitter
- `recvRaw()` returns correct byte count
- RSSI and SNR within expected range for distance/power used (e.g., −60 to −100 dBm at 1–10 m bench test)

### 5c. RSSI / SNR reporting

After RX, confirm mesh stats show non-zero RSSI and reasonable SNR.

```
getRSSI() — should not be 0.0 or −∞
getSNR()  — positive at short range, negative acceptable at sensitivity limit
```

### 5d. Random seed generation

`radio_get_rng_seed()` calls `radio.random(0x7FFFFFFF)`. Confirm non-zero return on clean init.

---

## 6. Dual-radio switching (meshtnc_ultra specific)

Tests the `setRadio()` + `select_radio()` + per-instance state fixes.

### 6a. Both radios initialise on boot

Serial must show both:
```
SX1281 ready
SX1276 ready
```
(or appropriate WARN if one fails — but not a hard fault).

### 6b. Active radio matches init result

If SX1281 succeeds: mesh operates on 2.4 GHz (verify with spectrum analyser or second SX1281-capable board).
If SX1281 fails: mesh falls back to SX1276 915 MHz automatically.

### 6c. No state bleed between wrappers

With SX1281 as active radio: confirm SX1276 DIO0 interrupt does not spuriously trigger `setFlag` on the 2.4 GHz wrapper. (Observe: no phantom `recvRaw` completions on the 915 MHz wrapper.)

---

## 7. Excluded module build flags

Confirm the base exclusions are still respected — these reduce binary size for all non-ultra variants:

```
RADIOLIB_EXCLUDE_CC1101
RADIOLIB_EXCLUDE_RF69
RADIOLIB_EXCLUDE_SX1231
RADIOLIB_EXCLUDE_SI443X
RADIOLIB_EXCLUDE_RFM2X
RADIOLIB_EXCLUDE_SX128X   ← must NOT be excluded on meshtnc_ultra
RADIOLIB_EXCLUDE_AFSK
RADIOLIB_EXCLUDE_AX25
...
```

**Test:** Build a non-ultra variant (e.g., `rak4631`) and confirm binary size is within 5% of the RadioLib baseline (check git log for last known flash usage before migration commit).

---

## 8. meshtnc_ultra crystal pre-warm redundancy check

`target.cpp` contains a manual FSK sleep → LoRa sleep → LoRa standby pre-warm sequence for the SX1276. With Preamble fix #2 in place this is redundant but harmless.

**Verify:** Remove the pre-warm block, rebuild, run cold-boot test (section 4a). If it still passes, the pre-warm can be removed. If it fails, keep it — the Preamble delay alone is insufficient for this board's crystal.

Document result here before removing the pre-warm code.

---

## Pass/Fail summary checklist

| # | Test | Result |
|---|------|--------|
| 1 | Full build matrix (38 variants) | |
| 2 | No RadioLib.h references in src/ | |
| 3a | SX1281 exact BW init | |
| 3b | SX1281 approximate BW init | |
| 4a | SX1276 cold-boot init (NRESET=NC) | |
| 4b | SX1276 warm-reset init | |
| 4c | No GPIO pre-transfer timeout (60s) | |
| 5a | TX completes without error | |
| 5b | RX receives correct packet | |
| 5c | RSSI/SNR non-zero and in range | |
| 5d | RNG seed non-zero | |
| 6a | Both radios print ready on boot | |
| 6b | Active radio matches init outcome | |
| 6c | No state bleed between wrappers | |
| 7 | Non-ultra binary size within 5% baseline | |
| 8 | Pre-warm redundancy (optional) | |
