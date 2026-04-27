# Intermediate steps and historical artifacts

This folder contains the files that show how the final T99W373 solution was reached.

These files are **not** the recommended install set. They are included so reviewers and testers can see the path from early PCIe experiments to the final ES and production bundles.

## Layout

```text
intermediate-steps/
  pcie-enabler-iterations/
  ipa-ipacm-experiments/
  ioss-rtk-diagnostic-modules/
  reports/
```

## `pcie-enabler-iterations/`

This folder contains intermediate `pcie_enabler` source and module builds.

Main progression:

| Version/file family | Purpose | Outcome |
|---|---|---|
| `pcie_enabler_ipaV2` | Early attempt to go beyond simple PCIe RC enable and start adding IPA/IOSS-related state. | Too aggressive for a stable base; useful as early proof of what had to be injected. |
| `pcie_enabler_ipaV5` | Continued runtime DT/IOSS property experiments. | Still unstable/incomplete; helped narrow the missing binding problem. |
| `pcie_enabler_ipaV6`, `V6.1`, `V6.2`, `V6.3` | Focused attempts to create a child RTL8125 node under the PCIe root port and improve OF node naming/linking. | PCIe/RTL8125 could be seen, but sysfs `of_node` linkage remained fragile. |
| `pcie_enabler_ipaV7` - `V11` | Iterations around channel properties, node shape, runtime OF behavior and IPA/IOSS assumptions. | Helped converge toward the V12-style full binding attempt. |
| `pcie_enabler_ipaV12` | Important full-profile runtime binding attempt: RTL8125 node, channel data and IOSS-related properties. | Key diagnostic milestone; later A/B testing separated real faults from teardown artifacts such as `rmmod r8125`. |
| `pcie_enabler_ipaV16` - `V21` | Crash-isolation series around IPA/SMMU/PMDS and `ipa_eth_client_conn_pipes`. | Showed that repeated pcie-only changes were not enough; the fault path involved IOSS/IPA mapping. |
| `pcie_enabler_ipaV22` | PMDS fix for IPA Ethernet SMMU nodes. | Important final ingredient; its logic was carried into `pcie_enabler_non_es_v1`. |
| `pcie_enabler_stepdiag*` | Step-by-step diagnostic module with runtime flags. | Used to isolate which parts of the V12-style profile were risky. |
| `pcie_enabler_v2` | Later consolidated experiment around the runtime enabler direction. | Kept for reference. |

Important lesson from this phase:

```text
The first milestone was not IPA. It was making the Realtek PCIe device visible.
The second milestone was making the kernel/IOSS/IPA view coherent enough not to crash.
```

## `ipa-ipacm-experiments/`

This folder contains the upload-side userspace experiments.

The final UL issue was not solved by changing the Realtek driver. The decisive bug was an ABI/layout mismatch between IPACM userspace and the kernel IPA interface.

Important families:

| File family | Purpose |
|---|---|
| `ipacm_abi_bridge_preload.c` | Early preload proof of concept. |
| `ipacm_abi_bridge_v2.c` - `ipacm_abi_bridge_v5.c` | Structured ABI bridge iterations that led to the final `libipacm_abi_bridge_final.so`. |
| `libipacm_abi_bridge_v1.so` - `libipacm_abi_bridge_v5.so` | Built bridge iterations used during live testing. |
| `libipacm_fix_nolibc_v*.so` | Earlier focused LD_PRELOAD experiments around route/filter/header/QMAP behavior. |
| `query_*_probe.c`, `copy_hdr_*`, `get_hdr_probe.c`, `add_hdr_probe_nolibc.c` | Small probes used to understand IPACM/kernel structure layouts and header behavior. |
| `force_ul_flt.c`, `notify_eth_wan.c`, `wan_up_add_tool.c` | Direct experiments around WAN/UL route and filter programming. |
| `source-tools/` | Full collected `.c` and `.sh` source/tool set from the IPA/IPACM experiment folder, included so the intermediate `.so` files are not the only historical evidence. |

The strongest clue in this phase was a numeric field reading:

```text
0x7670695f
```

That value is ASCII:

```text
"_ipv"
```

So IPACM was reading a string as an integer. That pointed to structure layout mismatch, not a simple missing route.

The final solution moved from broad hacks to a narrow ABI bridge with an interface allowlist:

```text
eth0
rmnet_data0
rmnet_data1
```

## `ioss-rtk-diagnostic-modules/`

This folder contains intermediate rebuilt modules used to isolate kernel-side behavior.

Families:

| Suffix | Meaning |
|---|---|
| `_dbgipa` | Added IPA-side debug logging. |
| `_phfix` | Phandle/fallback related experiments. |
| `_llccpa_fix` | Fixed LLCC physical address calculation. This corrected bad PA values but was not sufficient alone. |
| `_skipconn` | Built with/for skipping `ipa_eth_client_conn_pipes` to isolate the crash path. |
| `_skipreg` | Built with/for skipping full IPA registration. |
| `_diaghooks`, `_diag2` | Added extra diagnostic module parameters/hooks. |
| `_pow2guard` | Guarding/alignment/range experiments around ring/buffer constraints. |

Important finding:

```text
The durable crash path involved ioss_ipa_register -> ipa_eth_client_conn_pipes -> ipa3_eth_connect -> ipa3_smmu_map_eth_pipes -> av8l_fast_map.
```

That finding forced the work to pivot from only patching `pcie_enabler` to also instrumenting IOSS/IPA inputs and SMMU/PMDS state.

## `reports/`

This folder contains the explanatory reports that match the intermediate files.

Key files:

| Report | Use |
|---|---|
| `ipa_fix_progress_compatto.txt` | Chronological compact diary of the kernel/IPA debugging process. |
| `diag_report_2026-04-16.md` | Detailed SSR / IPA ETH offload diagnostic report. |
| `UL_ROOT_CAUSE_ABI_LAYOUT_2026-04-25.md` | ABI mismatch root-cause report. |
| `UL_ABI_BRIDGE_TEST_REPORT_2026-04-25.md` | First ABI bridge test report. |
| `UL_ABI_BRIDGE_V2_MATRIX_REPORT_2026-04-25.md` | Matrix testing for the V2 bridge. |
| `UL_ABI_BRIDGE_V3_REPORT_2026-04-25.md` | V3 bridge behavior and progress. |
| `UL_CRASH_DIAGNOSIS_V4_TO_V5_2026-04-26.md` | Why the bridge had to be narrowed/stabilized before final use. |

## What should be used today

For actual installs, use the final folders:

```text
../es-unlocked/
../production-locked/
../ipa-final/
```

Use this `intermediate-steps/` folder only for:

- understanding history;
- reviewing old attempts;
- bisecting regressions;
- explaining the project in articles;
- recovering an old diagnostic idea if a new firmware behaves differently.
