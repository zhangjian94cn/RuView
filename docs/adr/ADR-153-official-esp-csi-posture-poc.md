# ADR-153: Official ESP-CSI Data Plane for the Single-Person Posture PoC

| Field | Value |
|-------|-------|
| **Status** | Proposed; software implemented, field evidence pending |
| **Date** | 2026-07-28 |
| **Scope** | One fixed room, one person, three ESP32-S3 boards |
| **Supersedes** | ADR-152 before field acceptance |
| **Relates to** | ADR-018, ADR-151, ADR-152 |
| **Refined by** | ADR-154 |

## Context and Problem Statement

The custom controlled-link design in ADR-152 was built, but the room had not
yet produced reliable occupied-person evidence and the new firmware had not
been flashed. Espressif now provides maintained CSI sender/receiver examples
and an `esp_wifi_sensing` presence and motion component. The official
repository does not provide posture weights, skeleton inference, or a
production fall detector.

The experiment needs a trustworthy RF data owner and a separate owner for
room-specific posture semantics. It must never convert unsupported UI
skeletons or heuristic edge values into posture evidence.

Relevant implementation:

- [Official ESP-CSI fork at the verified build commit](https://github.com/zhangjian94cn/esp-csi/tree/e68ad8605570168a42e4eb4fcc28c87f5b2de34a)
- [Controlled transmitter](https://github.com/zhangjian94cn/esp-csi/tree/codex/s3-posture-poc/examples/ruview-posture/posture_tx)
- [Controlled receiver](https://github.com/zhangjian94cn/esp-csi/tree/codex/s3-posture-poc/examples/ruview-posture/posture_rx)
- [Mac posture tools](https://github.com/zhangjian94cn/esp-csi/tree/codex/s3-posture-poc/tools/ruview_posture)
- [Current RuView classification types](../../v2/crates/wifi-densepose-sensing-server/src/types.rs)
- [Firmware CI evidence](https://github.com/zhangjian94cn/esp-csi/actions/runs/30372497894)

## Decision Drivers

- Preserve the three verified 16 MB device backups and per-device identity.
- Reuse Espressif's CSI and gain-control implementation instead of maintaining
  a second low-level Wi-Fi stack.
- Keep posture training and inference on the Mac, where models and evidence can
  be versioned, replayed, and rejected safely.
- Treat sitting, standing, lying, and fall suspicion as room-specific research
  outputs rather than built-in ESP-CSI capabilities.
- Fail closed to `UNKNOWN` when either receiver link, topology, feature schema,
  or model binding is invalid.

## Considered Options

### Continue the custom RuView controlled probe

Rejected for this experiment. It duplicates functionality now available in the
official ESP-CSI repository and had not yet produced field evidence.

### Use only `esp_wifi_sensing`

Rejected as the complete solution. It is retained as the official
presence/motion benchmark, but it does not provide sitting, standing, lying,
or pose-keypoint inference.

### Official ESP-CSI data plane plus a local posture model

Selected. Node 1 sends a real-MAC HT20 50 Hz stream. Nodes 2 and 3 filter that
MAC and send versioned raw CSI to a discovered Mac sink. The Mac records
explicitly labelled trials and owns posture and fall-suspicion decisions.

### Add 60 GHz radar immediately

Deferred. Existing hardware receives one complete blind-test attempt first.
Failure after one retraining cycle is the mandatory trigger for the radar
fusion decision.

## Decision Outcome

ESP-CSI commit `8633d67152db2808f141cc1595970aa9cf406045`,
ESP-IDF `5.5.0`, and `esp_wifi_sensing` `0.1.1~2` are pinned. The official
examples remain unchanged as a baseline. The fork adds separate transmitter
and receiver applications and a versioned Mac recording protocol.

Fork commit `e68ad8605570168a42e4eb4fcc28c87f5b2de34a` passed the four-target
firmware matrix. Downloaded artifacts independently passed their SHA-256
manifests and flash-file reference checks. These CI artifacts contain only
virtual Wi-Fi configuration and are build evidence, not deployable household
firmware.

The standalone model output is `absent`, `present_still`,
`present_moving`, or `unknown`, with a separate posture value of `standing`,
`sitting`, `lying`, or `unknown`. Fall output is only `fall_suspected`, is
latched for ten seconds, and is marked research-only. RuView API/UI integration
is prohibited until the locked blind test and two-hour stability run pass.

[ADR-154](ADR-154-espectre-motion-baseline-esp-csi-posture.md) adds a
temporary ESPectre motion benchmark before this data plane is deployed. It
does not replace the ESP-CSI posture boundary or make ESPectre `IDLE` an
occupancy result.

## Consequences

- Firmware and model artifacts are maintained in the ESP-CSI fork, not copied
  into RuView.
- Wi-Fi credentials, raw household CSI, camera labels, topology, and trained
  models remain private and outside Git.
- Moving a board, changing the sender, or changing the RF topology invalidates
  the model.
- The current RuView runtime continues to report `UNKNOWN`; no synthetic
  skeleton, person count, vital signs, or fall claim is restored.
- Pure ESP-CSI posture remains experimental. Failure of the acceptance gates
  leads to 60 GHz radar fusion, not looser thresholds.

## Acceptance and Rollback

Acceptance requires both links at 40 Hz or better, no more than 5 percent
packet loss, posture macro F1 and per-class recall at 85 percent or better,
90 percent fall-trial recall, no more than 5 percent false fall trials, locked
blind evidence, and a two-hour stable run.

Before flashing, each board's existing 16 MB image must match its saved hash
and contain a valid partition table. Flash offsets come only from the selected
build's `flasher_args.json`. A failed board is restored from its own full image
before another board is touched.
