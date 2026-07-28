# ADR-152: Controlled-Link Supervised Presence Detection

| Field | Value |
|-------|-------|
| **Status** | Superseded before acceptance by ADR-153 |
| **Date** | 2026-07-27 |
| **Scope** | Fixed three-board presence and motion in one 15-30 m2 room |
| **Supersedes after acceptance** | ADR-151 |
| **Relates to** | ADR-018, ADR-039, ADR-060, ADR-135, ADR-151 |
| **Superseded by** | ADR-153 |

## Context and Problem Statement

The passive three-board deployment captures unrelated household Wi-Fi
transmitters and several CSI structures. The ADR-151 multimodal empty-room
baseline reduced false occupied reports, but a real occupied-room check showed
that its score remained below the learned entry thresholds. It cannot
distinguish absence from a stationary person reliably.

The room needs a deterministic sensing source and labeled evidence for both
sides of the decision. The system must publish `UNKNOWN` whenever the topology,
links, model, or evidence is not valid.

Relevant implementation:

- [ESP32 CSI collector](../../firmware/esp32-csi-node/main/csi_collector.c)
- [ESP32 NVS configuration](../../firmware/esp32-csi-node/main/nvs_config.c)
- [Sensing server](../../v2/crates/wifi-densepose-sensing-server/src/main.rs)
- [Signal processing crate](../../v2/crates/wifi-densepose-signal/src)
- [Experimental empty-room detector](../../v2/crates/wifi-densepose-sensing-server/src/single_link_presence.rs)
- [Observatory UI](../../ui)

## Decision Drivers

- Every accepted CSI frame must be attributable to one fixed transmitter.
- Two spatially separated links must cover the room's main activity area.
- Labels must be attached at ingestion time rather than inferred from names.
- Training, threshold selection, and blind testing must use separate sessions.
- Link, topology, feature, or model mismatch must fail closed to `UNKNOWN`.
- The deployed system must remain rollbackable to preserved firmware and data.

## Considered Options

### Continue passive household Wi-Fi capture

Rejected. Traffic source, rate, PHY, and subcarrier structure are uncontrolled,
so environment changes are confounded with human presence.

### Use the withdrawn upstream presence head directly

Rejected. Its reported result is not an accepted downstream presence
benchmark, and the available model card does not establish multi-class,
multi-room, or this-room accuracy.

### One fixed transmitter and two fixed receivers

Selected. Node 1 sends a 20 Hz probe stream. Nodes 2 and 3 accept CSI only from
node 1. Raw Null Data probes are preferred, with UDP broadcast as a measured
fallback when the 60-second link smoke test fails.

### Local supervised model with optional upstream encoder

Selected. A regularized logistic model is the baseline. The upstream v2
encoder is only a candidate feature extractor with newly trained local heads.
It is selected only when blocked cross-validation improves macro F1 by at
least two percentage points without regressing any hard gate.

## Decision Outcome

The fixed room topology uses node 1 as transmitter and nodes 2 and 3 as
receivers. A topology record binds node identities, MAC addresses, positions,
channel, environment, and target zones into a `topology_id`. Device status
packets must agree with this record before CSI is accepted for classification.

The server records complex CSI and explicit experiment metadata at UDP
ingestion. Features are computed on one-second windows with 250 ms steps using
the existing `PhaseSanitizer`, amplitude displacement, phase-difference
variance, temporal coherence, motion-band energy, RSSI change, and link
quality. Separate presence and motion heads produce the final state with
hysteresis.

The primary state is one of `absent`, `present_still`, `present_moving`,
`present_unknown`, or `unknown`. Before a model passes the locked blind test
and two-hour stability test, the primary output remains
`state=unknown`, `valid=false`, with presence and motion capabilities disabled.

## Consequences

- The result is scoped to one room and one fixed topology.
- Moving boards, changing channel, changing major furniture, or changing the
  feature schema invalidates the model.
- Data collection requires two labeled sessions plus a new blind session after
  every failed locked test.
- The firmware gains explicit probe-role and status contracts.
- The server owns model activation and truthfulness; edge values remain
  diagnostics until separately validated.
- Person count, pose, falls, heart rate, and breathing remain unsupported.
- The custom RuView probe implementation was built and validated in CI but was
  not flashed to the three devices. ADR-153 replaces it with an upstream
  Espressif ESP-CSI data plane before this proposal reached field acceptance.

## Acceptance and Supersession

ADR-152 becomes `accepted`, and ADR-151 becomes `superseded`, only when the
locked blind test meets all thresholds in the implementation plan and the
two-hour mixed stability run completes without crash, stream loss, drift, or
silent fallback. Model activation must be atomic and preserve the previous
firmware, model, topology, and dataset hashes for rollback.
