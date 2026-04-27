# IPA UL ABI bridge v3 report - 2026-04-25

## Executive summary

DL baseline remains preserved. After all v3 tests and rollback:

- `LD_PRELOAD` removed from `ipacm`.
- `pcie-enabler`, `ipacm`, `QCMAP_ConnectionManagerd`, `netmgrd`, `qcmap-radio-on` active.
- WAN route present via `rmnet_data1` at final check.
- Required modules still loaded: `rmnet_eth`, `r8125`, `ioss`, `r8125_ioss`.
- Host ping to `192.168.225.1` recovered after QCMAP settle.
- Host DL 1 KB over `enx00e04c6802a5` recovered with HTTP 200.

The UL root cause is now clearer than before: it is not a Realtek/IOSS/rmnet_eth driver problem. It is an ABI mismatch between IPACM userspace and the IPA kernel UAPI.

The v3 bridge proves a second ABI layer beyond `QUERY_TX/RX`: route/filter programming also needs translation because `struct ipa_rule_attrib` is 152 bytes in the running kernel ABI and 164 bytes in the IPACM/new header view.

## What v3 changed

v2 translated only interface property reads:

- `QUERY_INTF_TX_PROPS`: old 02300 -> new 13900 view.
- `QUERY_INTF_RX_PROPS`: old 02300 -> new 13900 view.
- `QUERY_INTF_EXT_PROPS`: log only, layout appears stable.

v3 keeps that and adds optional outgoing translation:

- `ADD_RT_RULE`: new route rule payload -> old route rule payload; copy handles/status back.
- `ADD_FLT_RULE`: new filter rule payload -> old filter rule payload; copy handles/status back.
- `GENERATE_FLT_EQ`: new attr input -> old attr input; copy generated equation back.

The new flag is:

```sh
IPA_ABI_BRIDGE_RT_FLT_TRANSLATE=1
```

This is not installed persistently. It was used only through a temporary `ipacm.service` drop-in and then removed.

## Files created

- `load_ipa_ES/ipacm_abi_bridge_v3.c`
- `load_ipa_ES/libipacm_abi_bridge_v3.so`
- `load_ipa_ES/UL_ABI_BRIDGE_V3_REPORT_2026-04-25.md`
- `load_ipa_ES/UL_CURRENT_DIAGNOSIS_2026-04-25.md`

Main artifact directory:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/abi_bridge_v3_test_2026-04-25_21-16-26/
```

## Test matrix

| phase | config | QCMAP restart | result | notes |
|---|---|---:|---|---|
| phase1 | eth0 TX/RX + route/filter translate | no, only ipacm | PASS | ping gateway OK, DL 1 KB HTTP 200 |
| phase2 | eth0 + rmnet_data0/1 TX/RX + EXT log + route/filter translate | yes | PARTIAL PASS | ping initially failed, DL 1 KB HTTP 200, upload 1 MB HTTP 200, rollback OK |
| phase3 | same as phase2 | yes | PARTIAL PASS | DL recovered under v3, upload 10 MB HTTP 200, rollback OK |

## Phase1 details

Artifact path:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/abi_bridge_v3_test_2026-04-25_21-16-26/phase1_eth0_ipacm_only/
```

Host watchdog:

- Ping `192.168.225.1`: 3/3 packets received.
- DL 1 KB: HTTP 200, 1024 bytes, about 3.4 s.

ABI log highlights:

```text
QUERY_TX if=eth0 action=translate_old_to_new
QUERY_RX if=eth0 action=translate_old_to_new
ADD_RT translated n=1 ret=0
ADD_FLT translated n=1 ret=0
ADD_FLT translated n=3 ret=0
ADD_FLT translated n=5 ret=0
```

Conclusion: v3 route/filter translation is not immediately destructive for the known-good eth0/LAN path.

## Phase2 details

Artifact path:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/abi_bridge_v3_test_2026-04-25_21-16-26/phase2_eth_rmnet_qcmap_rtflt_translate/
```

Host watchdog during active v3:

- Ping `192.168.225.1`: initially failed, 0/3.
- DL 1 KB: HTTP 200, 1024 bytes, about 13.94 s.
- Upload 1 MB: HTTP 200, 1048576 bytes uploaded, about 4.77 s.

Upload-window IPA counters:

| counter | before | after | delta |
|---|---:|---:|---:|
| `sw_tx` | 0 | 0 | 0 |
| `hw_tx` | 8635 | 8645 | +10 |
| `lan_rx_excp[SW_FILT]` | 8978 | 8982 | +4 |
| `lan_rx_excp[NAT]` | 3 | 9 | +6 |

Important: `SW_FILT` did not grow 1:1 with upload size, and `sw_tx` stayed zero. That is better than the original UL fallback behavior, but it is not enough by itself to certify UL HW 100%.

## Phase3 details

Artifact path:

```text
/home/manu/Scrivania/linaro 5.4/load_ipa_ES/abi_bridge_v3_test_2026-04-25_21-16-26/phase3_v3_10mb_upload_probe/
```

Connectivity wait under active v3:

- First DL probe recovered: HTTP 200, 1024 bytes, about 8.88 s.

Upload test:

- Upload 10 MB to Cloudflare `__up` endpoint.
- HTTP 200.
- Uploaded bytes: 10485760.
- Time: about 10.97 s.
- Observed curl average around 933 KB/s.

Upload-window IPA counters:

| counter | before | after | delta |
|---|---:|---:|---:|
| `sw_tx` | 0 | 0 | 0 |
| `hw_tx` | 72 | 76 | +4 |
| `tx_compl` | 72 | 76 | +4 |
| `wan_rx` | 0 | 0 | 0 |
| `lan_rx_excp[SW_FILT]` | 122 | 127 | +5 |
| `lan_rx_excp[NAT]` | 3 | 6 | +3 |
| `lan_rx_excp[NONE]` | 1 | 1 | 0 |

IPA message diff during 10 MB upload:

- No `WAN_UPSTREAM_ROUTE_ADD` increment observed.
- No `WAN_UPSTREAM_ROUTE_DEL` increment observed.

## What v3 proves

v3 proves these points:

1. The original `Failed to get QMAP header` is caused by ABI layout mismatch in `QUERY_INTF_TX_PROPS`/`QUERY_INTF_RX_PROPS`.
2. `rmnet_data0` and `rmnet_data1` properties are valid when read with the old layout:
   - TX dst pipe `35`.
   - RX src pipe `34`.
   - headers `dmux_hdr_v4_1` and `dmux_hdr_v4_2`.
   - mux ids `1` and `2` from EXT properties.
3. The second failure layer is route/filter ABI mismatch:
   - `ipa_rule_attrib`: 152 bytes old vs 164 bytes new.
   - `ipa_flt_rule_add`: 372 bytes old vs 384 bytes new.
   - multi-rule filters were previously sent with the wrong stride/status offsets.
4. With TX/RX plus route/filter translation, IPACM can keep DL/TCP forwarding alive and upload can complete through the modem.

## What v3 does not prove yet

UL is not declared 100% solved.

Reasons:

- `WAN_UPSTREAM_ROUTE_ADD W/R` still remained `0/0` in debugfs message counters.
- The upload HTTP probes pass, but the IPA counters do not yet show a large, obvious UL hardware packet movement equivalent to the DL `hw_tx` proof.
- QCMAP restart under active bridge causes a temporary LAN/ICMP blind window before settling.
- v3 is an LD_PRELOAD diagnostic bridge, not a clean product fix.
- The final solution should be an IPACM rebuild or internal compat layer, not a permanent preload.

## Current conclusion

Status:

```text
DL HW: stable baseline, preserved.
UL: root cause identified with high confidence; partial functional upload through v3; not yet certified as UL HW 100%.
```

Best next technical direction:

1. Port v3 logic into a real IPACM compat patch or rebuild IPACM against the exact 02300 IPA UAPI used by the running kernel.
2. Add better UL counters/tracing, because the current debugfs stats do not make UL HW proof as direct as DL.
3. Repeat upload with a stable non-preload IPACM and verify:
   - no `Failed to get QMAP header`,
   - valid `TriggerWANUp` IP and mux,
   - no route/filter add failures,
   - no long QCMAP blind window,
   - repeatable 10-50 MB upload,
   - DL still stable after reboot.
