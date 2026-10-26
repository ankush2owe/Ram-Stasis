# ENGINE JSON API - the exact UI <-> engine contract

Captured from real output of RamStasis.exe (commit 29ba4e1 + the --json stdout fix).
EVERY subcommand emits a SINGLE valid JSON document on stdout. The human-readable table goes to
stderr in --json mode. Errors go to stderr AND emit {"ok":false,"command":"...","error":"..."} on stdout.

    RamStasis.exe list    [--json]
    RamStasis.exe tree    <pid|exe> [--json]
    RamStasis.exe guard   [--json]
    RamStasis.exe freeze  <pid|exe> [--json] [--quiet] [--no-tree] [--force]
    RamStasis.exe thaw    <pid|exe> [--json] [--quiet]
    RamStasis.exe watch   <pid|exe> [--json] [--timeout ms]
    RamStasis.exe stats   <pid|exe> [--json] [--quiet]
    RamStasis.exe selftest [--json]

Exit codes: 0 ok | 2 bad usage/target | 3 no state file | 4 access denied | 5 tree/child failure
             6 resume imbalance | 7 window restore problem | 8 ANTI-CHEAT GUARD REFUSAL
             9 recycled pid | 10 another instance holds the lock

## `RamStasis.exe list --json`
`json
{
  "command": "list",
  "process_count": 157,
  "processes": [
    {
      "pid": 1008,
      "exe": "dwm.exe",
      "threads": 49,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 512790528,
        "peak_working_set": 728035328,
        "private_usage": 591056896,
        "pagefile_usage": 591056896,
        "peak_pagefile_usage": 902578176,
        "quota_paged": 1187752,
        "quota_nonpaged": 75944,
        "page_faults": 1109633
      }
    },
    {
      "pid": 15120,
      "exe": "Discord.exe",
      "threads": 57,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 499163136,
        "peak_working_set": 782499840,
        "private_usage": 747241472,
        "pagefile_usage": 747241472,
        "peak_pagefile_usage": 1080639488,
        "quota_paged": 1107840,
        "quota_nonpaged": 53320,
        "page_faults": 805571
      }
    },
    {
      "pid": 14576,
      "exe": "Discord.exe",
      "threads": 59,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 441614336,
        "peak_working_set": 764604416,
        "private_usage": 553566208,
        "pagefile_usage": 553566208,
        "peak_pagefile_usage": 701894656,
        "quota_paged": 1145832,
        "quota_nonpaged": 111448,
        "page_faults": 1247476
      }
    },
    {
      "pid": 2644,
      "exe": "opencode.exe",
      "threads": 34,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 369823744,
        "peak_working_set": 573796352,
        "private_usage": 763863040,
        "pagefile_usage": 763863040,
        "peak_pagefile_usage": 1015795712,
        "quota_paged": 561112,
        "quota_nonpaged": 28200,
        "page_faults": 7488642
      }
    },
    {
      "pid": 5588,
      "exe": "MsMpEng.exe",
      "threads": 55,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 281346048,
        "peak_working_set": 455643136,
        "private_usage": 422440960,
        "pagefile_usage": 422440960,
        "peak_pagefile_usage": 462950400,
        "quota_paged": 743576,
        "quota_nonpaged": 257016,
        "page_faults": 1207026
      }
    },
    {
      "pid": 13244,
      "exe": "opencode.exe",
      "threads": 30,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 180822016,
        "peak_working_set": 301846528,
        "private_usage": 549273600,
        "pagefile_usage": 549273600,
        "peak_pagefile_usage": 687652864,
        "quota_paged": 564176,
        "quota_nonpaged": 44200,
        "page_faults": 398325
      }
    },
    {
      "pid": 3240,
      "exe": "Memory Compression",
      "threads": 54,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 163078144,
        "peak_working_set": 191979520,
        "private_usage": 905216,
        "pagefile_usage": 905216,
        "peak_pagefile_usage": 925696,
        "quota_paged": 4224,
        "quota_nonpaged": 0,
        "page_faults": 254342
      }
    },
    {
      "pid": 7872,
      "exe": "explorer.exe",
      "threads": 97,
      "threads_suspended": 2,
      "suspend_state": "partial",
      "memory": {
        "ok": true,
        "working_set": 143904768,
        "peak_working_set": 301924352,
        "private_usage": 132128768,
        "pagefile_usage": 132128768,
        "peak_pagefile_usage": 210059264,
        "quota_paged": 2211144,
        "quota_nonpaged": 103200,
        "page_faults": 244250
      }
    },
    {
      "pid": 14916,
      "exe": "Discord.exe",
      "threads": 57,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 116903936,
        "peak_working_set": 300470272,
        "private_usage": 115982336,
        "pagefile_usage": 115982336,
        "peak_pagefile_usage": 246943744,
        "quota_paged": 951928,
        "quota_nonpaged": 55000,
        "page_faults": 576964
      }
    },
    {
      "pid": 8532,
      "exe": "StartMenuExperienceHost.exe",
      "threads": 29,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 83415040,
        "peak_working_set": 170971136,
        "private_usage": 91623424,
        "pagefile_usage": 91623424,
        "peak_pagefile_usage": 98648064,
        "quota_paged": 1274064,
        "quota_nonpaged": 50824,
        "page_faults": 56556
      }
    },
    {
      "pid": 8524,
      "exe": "SearchHost.exe",
      "threads": 24,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 79630336,
        "peak_working_set": 162627584,
        "private_usage": 68349952,
        "pagefile_usage": 68349952,
        "peak_pagefile_usage": 77471744,
        "quota_paged": 1750600,
        "quota_nonpaged": 52704,
        "page_faults": 61233
      }
    },
    {
      "pid": 5848,
      "exe": "powershell.exe",
      "threads": 29,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 74027008,
        "peak_working_set": 74043392,
        "private_usage": 62590976,
        "pagefile_usage": 62590976,
        "peak_pagefile_usage": 62672896,
        "quota_paged": 480024,
        "quota_nonpaged": 29896,
        "page_faults": 18840
      }
    },
    {
      "pid": 2840,
      "exe": "msedgewebview2.exe",
      "threads": 22,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 72863744,
        "peak_working_set": 134496256,
        "private_usage": 66666496,
        "pagefile_usage": 66666496,
        "peak_pagefile_usage": 81620992,
        "quota_paged": 935464,
        "quota_nonpaged": 25112,
        "page_faults": 57429
      }
    },
    {
      "pid": 2192,
      "exe": "svchost.exe",
      "threads": 38,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 56811520,
        "peak_working_set": 78000128,
        "private_usage": 20852736,
        "pagefile_usage": 20852736,
        "peak_pagefile_usage": 36720640,
        "quota_paged": 499672,
        "quota_nonpaged": 56016,
        "page_faults": 97733
      }
    },
    {
      "pid": 14600,
      "exe": "vctip.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 55365632,
        "peak_working_set": 60116992,
        "private_usage": 38408192,
        "pagefile_usage": 38408192,
        "peak_pagefile_usage": 39538688,
        "quota_paged": 483504,
        "quota_nonpaged": 34376,
        "page_faults": 48292
      }
    },
    {
      "pid": 15092,
      "exe": "Discord.exe",
      "threads": 17,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 54771712,
        "peak_working_set": 129826816,
        "private_usage": 21655552,
        "pagefile_usage": 21655552,
        "peak_pagefile_usage": 109256704,
        "quota_paged": 647240,
        "quota_nonpaged": 20504,
        "page_faults": 78527
      }
    },
    {
      "pid": 10320,
      "exe": "msedgewebview2.exe",
      "threads": 49,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 52645888,
        "peak_working_set": 128229376,
        "private_usage": 38240256,
        "pagefile_usage": 38240256,
        "peak_pagefile_usage": 41758720,
        "quota_paged": 1295704,
        "quota_nonpaged": 54632,
        "page_faults": 44933
      }
    },
    {
      "pid": 6072,
      "exe": "sihost.exe",
      "threads": 11,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 47190016,
        "peak_working_set": 52097024,
        "private_usage": 7860224,
        "pagefile_usage": 7860224,
        "peak_pagefile_usage": 8790016,
        "quota_paged": 378456,
        "quota_nonpaged": 24128,
        "page_faults": 15136
      }
    },
    {
      "pid": 6760,
      "exe": "svchost.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 43732992,
        "peak_working_set": 50671616,
        "private_usage": 9572352,
        "pagefile_usage": 9572352,
        "peak_pagefile_usage": 12402688,
        "quota_paged": 328008,
        "quota_nonpaged": 22432,
        "page_faults": 16534
      }
    },
    {
      "pid": 2588,
      "exe": "Notepad.exe",
      "threads": 79,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 41680896,
        "peak_working_set": 202182656,
        "private_usage": 144457728,
        "pagefile_usage": 144457728,
        "peak_pagefile_usage": 150691840,
        "quota_paged": 1118016,
        "quota_nonpaged": 87464,
        "page_faults": 80066
      }
    },
    {
      "pid": 5188,
      "exe": "TextInputHost.exe",
      "threads": 63,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 39686144,
        "peak_working_set": 250855424,
        "private_usage": 119099392,
        "pagefile_usage": 119099392,
        "peak_pagefile_usage": 162471936,
        "quota_paged": 1342864,
        "quota_nonpaged": 100408,
        "page_faults": 71155
      }
    },
    {
      "pid": 8844,
      "exe": "RuntimeBroker.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 39489536,
        "peak_working_set": 68853760,
        "private_usage": 13934592,
        "pagefile_usage": 13934592,
        "peak_pagefile_usage": 17350656,
        "quota_paged": 456928,
        "quota_nonpaged": 31256,
        "page_faults": 27338
      }
    },
    {
      "pid": 232,
      "exe": "Registry",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 39071744,
        "peak_working_set": 126066688,
        "private_usage": 6008832,
        "pagefile_usage": 6008832,
        "peak_pagefile_usage": 60895232,
        "quota_paged": 278720,
        "quota_nonpaged": 16184,
        "page_faults": 81364
      }
    },
    {
      "pid": 1864,
      "exe": "svchost.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 38158336,
        "peak_working_set": 43581440,
        "private_usage": 10797056,
        "pagefile_usage": 10797056,
        "peak_pagefile_usage": 12939264,
        "quota_paged": 548432,
        "quota_nonpaged": 30784,
        "page_faults": 14275
      }
    },
    {
      "pid": 11164,
      "exe": "msedgewebview2.exe",
      "threads": 33,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 36966400,
        "peak_working_set": 104955904,
        "private_usage": 88109056,
        "pagefile_usage": 88109056,
        "peak_pagefile_usage": 131612672,
        "quota_paged": 1171312,
        "quota_nonpaged": 34800,
        "page_faults": 50001
      }
    },
    {
      "pid": 14960,
      "exe": "Discord.exe",
      "threads": 7,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 35110912,
        "peak_working_set": 35778560,
        "private_usage": 9568256,
        "pagefile_usage": 9568256,
        "peak_pagefile_usage": 9768960,
        "quota_paged": 586624,
        "quota_nonpaged": 11208,
        "page_faults": 9594
      }
    },
    {
      "pid": 13332,
      "exe": "BridgeCommunication.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 33898496,
        "peak_working_set": 34009088,
        "private_usage": 5824512,
        "pagefile_usage": 5824512,
        "peak_pagefile_usage": 6959104,
        "quota_paged": 253240,
        "quota_nonpaged": 20448,
        "page_faults": 14600
      }
    },
    {
      "pid": 9876,
      "exe": "ctfmon.exe",
      "threads": 11,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 32190464,
        "peak_working_set": 33017856,
        "private_usage": 5894144,
        "pagefile_usage": 5894144,
        "peak_pagefile_usage": 6021120,
        "quota_paged": 265056,
        "quota_nonpaged": 19504,
        "page_faults": 9373
      }
    },
    {
      "pid": 11140,
      "exe": "msedgewebview2.exe",
      "threads": 18,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 32100352,
        "peak_working_set": 39153664,
        "private_usage": 11706368,
        "pagefile_usage": 11706368,
        "peak_pagefile_usage": 12316672,
        "quota_paged": 974032,
        "quota_nonpaged": 20048,
        "page_faults": 12132
      }
    },
    {
      "pid": 3460,
      "exe": "SysInfoCap.exe",
      "threads": 10,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 31633408,
        "peak_working_set": 38354944,
        "private_usage": 8151040,
        "pagefile_usage": 8151040,
        "peak_pagefile_usage": 10342400,
        "quota_paged": 217336,
        "quota_nonpaged": 22440,
        "page_faults": 21997
      }
    },
    {
      "pid": 2248,
      "exe": "DiagsCap.exe",
      "threads": 11,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 31219712,
        "peak_working_set": 31498240,
        "private_usage": 5545984,
        "pagefile_usage": 5545984,
        "peak_pagefile_usage": 5853184,
        "quota_paged": 204456,
        "quota_nonpaged": 20584,
        "page_faults": 8737
      }
    },
    {
      "pid": 12232,
      "exe": "RuntimeBroker.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 30928896,
        "peak_working_set": 36040704,
        "private_usage": 4849664,
        "pagefile_usage": 4849664,
        "peak_pagefile_usage": 6103040,
        "quota_paged": 257104,
        "quota_nonpaged": 16184,
        "page_faults": 9702
      }
    },
    {
      "pid": 3028,
      "exe": "NVDisplay.Container.exe",
      "threads": 36,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 29667328,
        "peak_working_set": 63799296,
        "private_usage": 27828224,
        "pagefile_usage": 27828224,
        "peak_pagefile_usage": 40148992,
        "quota_paged": 295992,
        "quota_nonpaged": 24672,
        "page_faults": 2404364
      }
    },
    {
      "pid": 3468,
      "exe": "AppHelperCap.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 29540352,
        "peak_working_set": 33681408,
        "private_usage": 5287936,
        "pagefile_usage": 5287936,
        "peak_pagefile_usage": 6512640,
        "quota_paged": 205304,
        "quota_nonpaged": 19112,
        "page_faults": 13518
      }
    },
    {
      "pid": 2056,
      "exe": "svchost.exe",
      "threads": 23,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 29097984,
        "peak_working_set": 89493504,
        "private_usage": 8056832,
        "pagefile_usage": 8056832,
        "peak_pagefile_usage": 64561152,
        "quota_paged": 211024,
        "quota_nonpaged": 35736,
        "page_faults": 79304
      }
    },
    {
      "pid": 3444,
      "exe": "NetworkCap.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 28958720,
        "peak_working_set": 33189888,
        "private_usage": 5480448,
        "pagefile_usage": 5480448,
        "peak_pagefile_usage": 6569984,
        "quota_paged": 202840,
        "quota_nonpaged": 19040,
        "page_faults": 11581
      }
    },
    {
      "pid": 12284,
      "exe": "SearchIndexer.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 28405760,
        "peak_working_set": 37019648,
        "private_usage": 17027072,
        "pagefile_usage": 17027072,
        "peak_pagefile_usage": 24788992,
        "quota_paged": 190440,
        "quota_nonpaged": 18928,
        "page_faults": 18003
      }
    },
    {
      "pid": 4588,
      "exe": "svchost.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 27377664,
        "peak_working_set": 41902080,
        "private_usage": 7950336,
        "pagefile_usage": 7950336,
        "peak_pagefile_usage": 10711040,
        "quota_paged": 295632,
        "quota_nonpaged": 20680,
        "page_faults": 11653
      }
    },
    {
      "pid": 5580,
      "exe": "wslservice.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 26918912,
        "peak_working_set": 29827072,
        "private_usage": 5664768,
        "pagefile_usage": 5664768,
        "peak_pagefile_usage": 7208960,
        "quota_paged": 243736,
        "quota_nonpaged": 21040,
        "page_faults": 8573
      }
    },
    {
      "pid": 3864,
      "exe": "audiodg.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 26869760,
        "peak_working_set": 66785280,
        "private_usage": 62304256,
        "pagefile_usage": 62304256,
        "peak_pagefile_usage": 63512576,
        "quota_paged": 174360,
        "quota_nonpaged": 16784,
        "page_faults": 40717
      }
    },
    {
      "pid": 8852,
      "exe": "svchost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 26750976,
        "peak_working_set": 33353728,
        "private_usage": 4112384,
        "pagefile_usage": 4112384,
        "peak_pagefile_usage": 6909952,
        "quota_paged": 233520,
        "quota_nonpaged": 16056,
        "page_faults": 9515
      }
    },
    {
      "pid": 3452,
      "exe": "TouchpointAnalyticsClientService.exe",
      "threads": 11,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 24936448,
        "peak_working_set": 89415680,
        "private_usage": 63492096,
        "pagefile_usage": 63492096,
        "peak_pagefile_usage": 65081344,
        "quota_paged": 557032,
        "quota_nonpaged": 48808,
        "page_faults": 74635
      }
    },
    {
      "pid": 10912,
      "exe": "RuntimeBroker.exe",
      "threads": 14,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 24535040,
        "peak_working_set": 27525120,
        "private_usage": 4489216,
        "pagefile_usage": 4489216,
        "peak_pagefile_usage": 5525504,
        "quota_paged": 204400,
        "quota_nonpaged": 15952,
        "page_faults": 7592
      }
    },
    {
      "pid": 5568,
      "exe": "senseshield.exe",
      "threads": 39,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 24133632,
        "peak_working_set": 30642176,
        "private_usage": 10371072,
        "pagefile_usage": 10371072,
        "peak_pagefile_usage": 15036416,
        "quota_paged": 185296,
        "quota_nonpaged": 31928,
        "page_faults": 10875
      }
    },
    {
      "pid": 2272,
      "exe": "svchost.exe",
      "threads": 12,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 24129536,
        "peak_working_set": 35909632,
        "private_usage": 20434944,
        "pagefile_usage": 20434944,
        "peak_pagefile_usage": 25546752,
        "quota_paged": 139584,
        "quota_nonpaged": 20872,
        "page_faults": 17710
      }
    },
    {
      "pid": 3156,
      "exe": "svchost.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 23941120,
        "peak_working_set": 26415104,
        "private_usage": 6868992,
        "pagefile_usage": 6868992,
        "peak_pagefile_usage": 8761344,
        "quota_paged": 197864,
        "quota_nonpaged": 30312,
        "page_faults": 8925
      }
    },
    {
      "pid": 7824,
      "exe": "CrossDeviceResume.exe",
      "threads": 19,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 23904256,
        "peak_working_set": 72957952,
        "private_usage": 33898496,
        "pagefile_usage": 33898496,
        "peak_pagefile_usage": 35909632,
        "quota_paged": 625784,
        "quota_nonpaged": 32912,
        "page_faults": 19396
      }
    },
    {
      "pid": 2088,
      "exe": "svchost.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 23093248,
        "peak_working_set": 29687808,
        "private_usage": 8679424,
        "pagefile_usage": 8679424,
        "peak_pagefile_usage": 12931072,
        "quota_paged": 247888,
        "quota_nonpaged": 37776,
        "page_faults": 9764
      }
    },
    {
      "pid": 2592,
      "exe": "Discord.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22921216,
        "peak_working_set": 100114432,
        "private_usage": 10776576,
        "pagefile_usage": 10776576,
        "peak_pagefile_usage": 11001856,
        "quota_paged": 608136,
        "quota_nonpaged": 15080,
        "page_faults": 25147
      }
    },
    {
      "pid": 1736,
      "exe": "lsass.exe",
      "threads": 10,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22642688,
        "peak_working_set": 28839936,
        "private_usage": 9543680,
        "pagefile_usage": 9543680,
        "peak_pagefile_usage": 10276864,
        "quota_paged": 209016,
        "quota_nonpaged": 25480,
        "page_faults": 12931
      }
    },
    {
      "pid": 10336,
      "exe": "WidgetService.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22368256,
        "peak_working_set": 25276416,
        "private_usage": 4718592,
        "pagefile_usage": 4718592,
        "peak_pagefile_usage": 5545984,
        "quota_paged": 212032,
        "quota_nonpaged": 16624,
        "page_faults": 6870
      }
    },
    {
      "pid": 12840,
      "exe": "conhost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22347776,
        "peak_working_set": 22876160,
        "private_usage": 3108864,
        "pagefile_usage": 3108864,
        "peak_pagefile_usage": 3309568,
        "quota_paged": 232952,
        "quota_nonpaged": 15912,
        "page_faults": 6602
      }
    },
    {
      "pid": 13152,
      "exe": "RuntimeBroker.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22233088,
        "peak_working_set": 29204480,
        "private_usage": 4984832,
        "pagefile_usage": 4984832,
        "peak_pagefile_usage": 5861376,
        "quota_paged": 212152,
        "quota_nonpaged": 14696,
        "page_faults": 7612
      }
    },
    {
      "pid": 12752,
      "exe": "dllhost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22085632,
        "peak_working_set": 22822912,
        "private_usage": 2985984,
        "pagefile_usage": 2985984,
        "peak_pagefile_usage": 3334144,
        "quota_paged": 201296,
        "quota_nonpaged": 12520,
        "page_faults": 6143
      }
    },
    {
      "pid": 2076,
      "exe": "backgroundTaskHost.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 22081536,
        "peak_working_set": 67932160,
        "private_usage": 24956928,
        "pagefile_usage": 24956928,
        "peak_pagefile_usage": 27205632,
        "quota_paged": 638336,
        "quota_nonpaged": 21104,
        "page_faults": 18289
      }
    },
    {
      "pid": 4932,
      "exe": "svchost.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 21921792,
        "peak_working_set": 25751552,
        "private_usage": 6537216,
        "pagefile_usage": 6537216,
        "peak_pagefile_usage": 8085504,
        "quota_paged": 183104,
        "quota_nonpaged": 24320,
        "page_faults": 7808
      }
    },
    {
      "pid": 11512,
      "exe": "FxSound.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 21733376,
        "peak_working_set": 33169408,
        "private_usage": 16105472,
        "pagefile_usage": 16105472,
        "peak_pagefile_usage": 16605184,
        "quota_paged": 224312,
        "quota_nonpaged": 16848,
        "page_faults": 9788
      }
    },
    {
      "pid": 7940,
      "exe": "ShellHost.exe",
      "threads": 19,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 21323776,
        "peak_working_set": 65253376,
        "private_usage": 32264192,
        "pagefile_usage": 32264192,
        "peak_pagefile_usage": 32342016,
        "quota_paged": 594848,
        "quota_nonpaged": 28560,
        "page_faults": 16999
      }
    },
    {
      "pid": 15940,
      "exe": "svchost.exe",
      "threads": 11,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 21286912,
        "peak_working_set": 31305728,
        "private_usage": 13377536,
        "pagefile_usage": 13377536,
        "peak_pagefile_usage": 17776640,
        "quota_paged": 117664,
        "quota_nonpaged": 15920,
        "page_faults": 14220
      }
    },
    {
      "pid": 8436,
      "exe": "ShellExperienceHost.exe",
      "threads": 26,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 21004288,
        "peak_working_set": 94363648,
        "private_usage": 39534592,
        "pagefile_usage": 39534592,
        "peak_pagefile_usage": 40734720,
        "quota_paged": 783088,
        "quota_nonpaged": 35128,
        "page_faults": 25710
      }
    },
    {
      "pid": 10216,
      "exe": "svchost.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 20602880,
        "peak_working_set": 21504000,
        "private_usage": 3145728,
        "pagefile_usage": 3145728,
        "peak_pagefile_usage": 3706880,
        "quota_paged": 206200,
        "quota_nonpaged": 12792,
        "page_faults": 6326
      }
    },
    {
      "pid": 7248,
      "exe": "svchost.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 20590592,
        "peak_working_set": 28053504,
        "private_usage": 3604480,
        "pagefile_usage": 3604480,
        "peak_pagefile_usage": 4993024,
        "quota_paged": 155608,
        "quota_nonpaged": 14152,
        "page_faults": 8988
      }
    },
    {
      "pid": 13736,
      "exe": "node.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 20529152,
        "peak_working_set": 65511424,
        "private_usage": 29220864,
        "pagefile_usage": 29220864,
        "peak_pagefile_usage": 48701440,
        "quota_paged": 332464,
        "quota_nonpaged": 20864,
        "page_faults": 20761
      }
    },
    {
      "pid": 3780,
      "exe": "svchost.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 20332544,
        "peak_working_set": 34131968,
        "private_usage": 9998336,
        "pagefile_usage": 9998336,
        "peak_pagefile_usage": 23117824,
        "quota_paged": 96240,
        "quota_nonpaged": 10072,
        "page_faults": 18770
      }
    },
    {
      "pid": 6864,
      "exe": "svchost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 20066304,
        "peak_working_set": 27541504,
        "private_usage": 4673536,
        "pagefile_usage": 4673536,
        "peak_pagefile_usage": 6258688,
        "quota_paged": 159192,
        "quota_nonpaged": 15008,
        "page_faults": 16657
      }
    },
    {
      "pid": 13740,
      "exe": "node.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 19804160,
        "peak_working_set": 65773568,
        "private_usage": 29585408,
        "pagefile_usage": 29585408,
        "peak_pagefile_usage": 48959488,
        "quota_paged": 332464,
        "quota_nonpaged": 21000,
        "page_faults": 21674
      }
    },
    {
      "pid": 4596,
      "exe": "svchost.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 19677184,
        "peak_working_set": 33107968,
        "private_usage": 11468800,
        "pagefile_usage": 11468800,
        "peak_pagefile_usage": 19243008,
        "quota_paged": 136296,
        "quota_nonpaged": 34360,
        "page_faults": 13662
      }
    },
    {
      "pid": 2488,
      "exe": "svchost.exe",
      "threads": 10,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 19456000,
        "peak_working_set": 23334912,
        "private_usage": 5652480,
        "pagefile_usage": 5652480,
        "peak_pagefile_usage": 8908800,
        "quota_paged": 166904,
        "quota_nonpaged": 21088,
        "page_faults": 8236
      }
    },
    {
      "pid": 5064,
      "exe": "spoolsv.exe",
      "threads": 7,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 18796544,
        "peak_working_set": 21889024,
        "private_usage": 5763072,
        "pagefile_usage": 5763072,
        "peak_pagefile_usage": 6373376,
        "quota_paged": 154336,
        "quota_nonpaged": 21728,
        "page_faults": 5997
      }
    },
    {
      "pid": 4240,
      "exe": "NVDisplay.Container.exe",
      "threads": 26,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 18669568,
        "peak_working_set": 237793280,
        "private_usage": 37863424,
        "pagefile_usage": 37863424,
        "peak_pagefile_usage": 213118976,
        "quota_paged": 435096,
        "quota_nonpaged": 32016,
        "page_faults": 140499
      }
    },
    {
      "pid": 3200,
      "exe": "atieclxx.exe",
      "threads": 14,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 18497536,
        "peak_working_set": 22544384,
        "private_usage": 5308416,
        "pagefile_usage": 5308416,
        "peak_pagefile_usage": 5636096,
        "quota_paged": 254592,
        "quota_nonpaged": 18864,
        "page_faults": 7531
      }
    },
    {
      "pid": 2256,
      "exe": "svchost.exe",
      "threads": 17,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 18333696,
        "peak_working_set": 20373504,
        "private_usage": 8159232,
        "pagefile_usage": 8159232,
        "peak_pagefile_usage": 8736768,
        "quota_paged": 154976,
        "quota_nonpaged": 19280,
        "page_faults": 12994
      }
    },
    {
      "pid": 3564,
      "exe": "svchost.exe",
      "threads": 6,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 18313216,
        "peak_working_set": 19566592,
        "private_usage": 3764224,
        "pagefile_usage": 3764224,
        "peak_pagefile_usage": 4927488,
        "quota_paged": 134560,
        "quota_nonpaged": 14000,
        "page_faults": 5707
      }
    },
    {
      "pid": 6472,
      "exe": "node.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17899520,
        "peak_working_set": 95432704,
        "private_usage": 75501568,
        "pagefile_usage": 75501568,
        "peak_pagefile_usage": 114130944,
        "quota_paged": 337816,
        "quota_nonpaged": 25464,
        "page_faults": 30221
      }
    },
    {
      "pid": 11212,
      "exe": "node.exe",
      "threads": 16,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17858560,
        "peak_working_set": 74153984,
        "private_usage": 83591168,
        "pagefile_usage": 83591168,
        "peak_pagefile_usage": 101773312,
        "quota_paged": 674720,
        "quota_nonpaged": 28184,
        "page_faults": 25701
      }
    },
    {
      "pid": 1708,
      "exe": "services.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17641472,
        "peak_working_set": 20869120,
        "private_usage": 5775360,
        "pagefile_usage": 5775360,
        "peak_pagefile_usage": 8368128,
        "quota_paged": 232480,
        "quota_nonpaged": 18448,
        "page_faults": 9900
      }
    },
    {
      "pid": 6872,
      "exe": "svchost.exe",
      "threads": 27,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17571840,
        "peak_working_set": 31580160,
        "private_usage": 14389248,
        "pagefile_usage": 14389248,
        "peak_pagefile_usage": 19111936,
        "quota_paged": 106328,
        "quota_nonpaged": 17888,
        "page_faults": 12946
      }
    },
    {
      "pid": 10284,
      "exe": "msedgewebview2.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17547264,
        "peak_working_set": 20754432,
        "private_usage": 7876608,
        "pagefile_usage": 7876608,
        "peak_pagefile_usage": 7970816,
        "quota_paged": 888936,
        "quota_nonpaged": 12520,
        "page_faults": 6210
      }
    },
    {
      "pid": 11884,
      "exe": "RtkAudUService64.exe",
      "threads": 16,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17534976,
        "peak_working_set": 20406272,
        "private_usage": 26652672,
        "pagefile_usage": 26652672,
        "peak_pagefile_usage": 27209728,
        "quota_paged": 261960,
        "quota_nonpaged": 15768,
        "page_faults": 5967
      }
    },
    {
      "pid": 9348,
      "exe": "LockApp.exe",
      "threads": 30,
      "threads_suspended": 30,
      "suspend_state": "SUSPENDED",
      "memory": {
        "ok": true,
        "working_set": 17502208,
        "peak_working_set": 96448512,
        "private_usage": 40067072,
        "pagefile_usage": 40067072,
        "peak_pagefile_usage": 40955904,
        "quota_paged": 820400,
        "quota_nonpaged": 37712,
        "page_faults": 26720
      }
    },
    {
      "pid": 13196,
      "exe": "RuntimeBroker.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 17104896,
        "peak_working_set": 19808256,
        "private_usage": 2572288,
        "pagefile_usage": 2572288,
        "peak_pagefile_usage": 3076096,
        "quota_paged": 181456,
        "quota_nonpaged": 11432,
        "page_faults": 5271
      }
    },
    {
      "pid": 9572,
      "exe": "RtkAudUService64.exe",
      "threads": 6,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 16429056,
        "peak_working_set": 19267584,
        "private_usage": 25911296,
        "pagefile_usage": 25911296,
        "peak_pagefile_usage": 26222592,
        "quota_paged": 255032,
        "quota_nonpaged": 14144,
        "page_faults": 5546
      }
    },
    {
      "pid": 7560,
      "exe": "taskhostw.exe",
      "threads": 7,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 16355328,
        "peak_working_set": 17121280,
        "private_usage": 4579328,
        "pagefile_usage": 4579328,
        "peak_pagefile_usage": 4747264,
        "quota_paged": 183456,
        "quota_nonpaged": 25504,
        "page_faults": 5057
      }
    },
    {
      "pid": 1996,
      "exe": "svchost.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 16060416,
        "peak_working_set": 17395712,
        "private_usage": 6905856,
        "pagefile_usage": 6905856,
        "peak_pagefile_usage": 7614464,
        "quota_paged": 150832,
        "quota_nonpaged": 21064,
        "page_faults": 21415
      }
    },
    {
      "pid": 5520,
      "exe": "RtkAudUService64.exe",
      "threads": 20,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 16007168,
        "peak_working_set": 19951616,
        "private_usage": 27082752,
        "pagefile_usage": 27082752,
        "peak_pagefile_usage": 27582464,
        "quota_paged": 219672,
        "quota_nonpaged": 16192,
        "page_faults": 6033
      }
    },
    {
      "pid": 12712,
      "exe": "dllhost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 15896576,
        "peak_working_set": 16281600,
        "private_usage": 2011136,
        "pagefile_usage": 2011136,
        "peak_pagefile_usage": 2322432,
        "quota_paged": 151016,
        "quota_nonpaged": 10072,
        "page_faults": 4374
      }
    },
    {
      "pid": 5560,
      "exe": "svchost.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 15491072,
        "peak_working_set": 17629184,
        "private_usage": 6791168,
        "pagefile_usage": 6791168,
        "peak_pagefile_usage": 7761920,
        "quota_paged": 110224,
        "quota_nonpaged": 13536,
        "page_faults": 6182
      }
    },
    {
      "pid": 5904,
      "exe": "ETDCtrl.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 15433728,
        "peak_working_set": 19562496,
        "private_usage": 6897664,
        "pagefile_usage": 6897664,
        "peak_pagefile_usage": 7102464,
        "quota_paged": 211728,
        "quota_nonpaged": 14336,
        "page_faults": 5489
      }
    },
    {
      "pid": 11860,
      "exe": "NisSrv.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 14393344,
        "peak_working_set": 17301504,
        "private_usage": 4657152,
        "pagefile_usage": 4657152,
        "peak_pagefile_usage": 5689344,
        "quota_paged": 139424,
        "quota_nonpaged": 20424,
        "page_faults": 5276
      }
    },
    {
      "pid": 5544,
      "exe": "MpDefenderCoreService.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 14360576,
        "peak_working_set": 30351360,
        "private_usage": 12124160,
        "pagefile_usage": 12124160,
        "peak_pagefile_usage": 13094912,
        "quota_paged": 186408,
        "quota_nonpaged": 17864,
        "page_faults": 30784
      }
    },
    {
      "pid": 16204,
      "exe": "svchost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13881344,
        "peak_working_set": 14245888,
        "private_usage": 2502656,
        "pagefile_usage": 2502656,
        "peak_pagefile_usage": 2957312,
        "quota_paged": 113064,
        "quota_nonpaged": 10344,
        "page_faults": 3956
      }
    },
    {
      "pid": 4196,
      "exe": "DiscordSystemHelper.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13824000,
        "peak_working_set": 37097472,
        "private_usage": 3465216,
        "pagefile_usage": 3465216,
        "peak_pagefile_usage": 4075520,
        "quota_paged": 187848,
        "quota_nonpaged": 11992,
        "page_faults": 111833
      }
    },
    {
      "pid": 7184,
      "exe": "svchost.exe",
      "threads": 9,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13709312,
        "peak_working_set": 14434304,
        "private_usage": 3284992,
        "pagefile_usage": 3284992,
        "peak_pagefile_usage": 3932160,
        "quota_paged": 89584,
        "quota_nonpaged": 13512,
        "page_faults": 4237
      }
    },
    {
      "pid": 2264,
      "exe": "svchost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13692928,
        "peak_working_set": 14893056,
        "private_usage": 2621440,
        "pagefile_usage": 2621440,
        "peak_pagefile_usage": 3031040,
        "quota_paged": 124408,
        "quota_nonpaged": 11160,
        "page_faults": 4174
      }
    },
    {
      "pid": 6428,
      "exe": "SECOCL64.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13578240,
        "peak_working_set": 15429632,
        "private_usage": 2813952,
        "pagefile_usage": 2813952,
        "peak_pagefile_usage": 2998272,
        "quota_paged": 217056,
        "quota_nonpaged": 12184,
        "page_faults": 4079
      }
    },
    {
      "pid": 5536,
      "exe": "SECOMN64.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13438976,
        "peak_working_set": 18468864,
        "private_usage": 4931584,
        "pagefile_usage": 4931584,
        "peak_pagefile_usage": 5107712,
        "quota_paged": 178568,
        "quota_nonpaged": 12552,
        "page_faults": 4982
      }
    },
    {
      "pid": 7648,
      "exe": "DiscordSystemHelper.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13271040,
        "peak_working_set": 36577280,
        "private_usage": 3567616,
        "pagefile_usage": 3567616,
        "peak_pagefile_usage": 3866624,
        "quota_paged": 184208,
        "quota_nonpaged": 11584,
        "page_faults": 111647
      }
    },
    {
      "pid": 1636,
      "exe": "winlogon.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 13037568,
        "peak_working_set": 26234880,
        "private_usage": 2727936,
        "pagefile_usage": 2727936,
        "peak_pagefile_usage": 4308992,
        "quota_paged": 160800,
        "quota_nonpaged": 1323656,
        "page_faults": 10613
      }
    },
    {
      "pid": 4672,
      "exe": "WmiPrvSE.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12939264,
        "peak_working_set": 16584704,
        "private_usage": 3137536,
        "pagefile_usage": 3137536,
        "peak_pagefile_usage": 6885376,
        "quota_paged": 86016,
        "quota_nonpaged": 13232,
        "page_faults": 5466
      }
    },
    {
      "pid": 13516,
      "exe": "node.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12853248,
        "peak_working_set": 93970432,
        "private_usage": 76902400,
        "pagefile_usage": 76902400,
        "peak_pagefile_usage": 111579136,
        "quota_paged": 337816,
        "quota_nonpaged": 25600,
        "page_faults": 29481
      }
    },
    {
      "pid": 6980,
      "exe": "cowork-svc.exe",
      "threads": 11,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12685312,
        "peak_working_set": 36110336,
        "private_usage": 25649152,
        "pagefile_usage": 25649152,
        "peak_pagefile_usage": 26062848,
        "quota_paged": 156384,
        "quota_nonpaged": 14680,
        "page_faults": 10475
      }
    },
    {
      "pid": 2692,
      "exe": "svchost.exe",
      "threads": 6,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12541952,
        "peak_working_set": 15003648,
        "private_usage": 2940928,
        "pagefile_usage": 2940928,
        "peak_pagefile_usage": 4902912,
        "quota_paged": 115344,
        "quota_nonpaged": 14304,
        "page_faults": 4147
      }
    },
    {
      "pid": 10648,
      "exe": "msedgewebview2.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12431360,
        "peak_working_set": 18862080,
        "private_usage": 3047424,
        "pagefile_usage": 3047424,
        "peak_pagefile_usage": 3637248,
        "quota_paged": 138616,
        "quota_nonpaged": 11296,
        "page_faults": 5269
      }
    },
    {
      "pid": 3064,
      "exe": "svchost.exe",
      "threads": 6,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12288000,
        "peak_working_set": 15859712,
        "private_usage": 3866624,
        "pagefile_usage": 3866624,
        "peak_pagefile_usage": 4739072,
        "quota_paged": 112248,
        "quota_nonpaged": 12840,
        "page_faults": 6479
      }
    },
    {
      "pid": 2680,
      "exe": "node.exe",
      "threads": 16,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 12062720,
        "peak_working_set": 75014144,
        "private_usage": 84832256,
        "pagefile_usage": 84832256,
        "peak_pagefile_usage": 102051840,
        "quota_paged": 674720,
        "quota_nonpaged": 28320,
        "page_faults": 25730
      }
    },
    {
      "pid": 3360,
      "exe": "svchost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 11587584,
        "peak_working_set": 12390400,
        "private_usage": 2146304,
        "pagefile_usage": 2146304,
        "peak_pagefile_usage": 2707456,
        "quota_paged": 90672,
        "quota_nonpaged": 13200,
        "page_faults": 3584
      }
    },
    {
      "pid": 2176,
      "exe": "svchost.exe",
      "threads": 10,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 11210752,
        "peak_working_set": 13049856,
        "private_usage": 3395584,
        "pagefile_usage": 3395584,
        "peak_pagefile_usage": 4448256,
        "quota_paged": 102856,
        "quota_nonpaged": 16968,
        "page_faults": 7869
      }
    },
    {
      "pid": 7828,
      "exe": "svchost.exe",
      "threads": 8,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 11169792,
        "peak_working_set": 12357632,
        "private_usage": 2748416,
        "pagefile_usage": 2748416,
        "peak_pagefile_usage": 3293184,
        "quota_paged": 93832,
        "quota_nonpaged": 11912,
        "page_faults": 3345
      }
    },
    {
      "pid": 1852,
      "exe": "svchost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10690560,
        "peak_working_set": 11657216,
        "private_usage": 2150400,
        "pagefile_usage": 2150400,
        "peak_pagefile_usage": 2592768,
        "quota_paged": 120320,
        "quota_nonpaged": 9256,
        "page_faults": 3226
      }
    },
    {
      "pid": 3216,
      "exe": "svchost.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10461184,
        "peak_working_set": 11505664,
        "private_usage": 2142208,
        "pagefile_usage": 2142208,
        "peak_pagefile_usage": 2785280,
        "quota_paged": 83536,
        "quota_nonpaged": 10400,
        "page_faults": 3313
      }
    },
    {
      "pid": 2872,
      "exe": "OmenCap.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10440704,
        "peak_working_set": 12455936,
        "private_usage": 2260992,
        "pagefile_usage": 2260992,
        "peak_pagefile_usage": 2482176,
        "quota_paged": 110280,
        "quota_nonpaged": 11904,
        "page_faults": 3290
      }
    },
    {
      "pid": 10632,
      "exe": "svchost.exe",
      "threads": 6,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10424320,
        "peak_working_set": 11452416,
        "private_usage": 2260992,
        "pagefile_usage": 2260992,
        "peak_pagefile_usage": 2871296,
        "quota_paged": 88720,
        "quota_nonpaged": 16520,
        "page_faults": 3150
      }
    },
    {
      "pid": 5616,
      "exe": "virboxlcc.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10182656,
        "peak_working_set": 14483456,
        "private_usage": 2793472,
        "pagefile_usage": 2793472,
        "peak_pagefile_usage": 3088384,
        "quota_paged": 142936,
        "quota_nonpaged": 14072,
        "page_faults": 3878
      }
    },
    {
      "pid": 5292,
      "exe": "svchost.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10092544,
        "peak_working_set": 11337728,
        "private_usage": 1990656,
        "pagefile_usage": 1990656,
        "peak_pagefile_usage": 2457600,
        "quota_paged": 93600,
        "quota_nonpaged": 11104,
        "page_faults": 3194
      }
    },
    {
      "pid": 8056,
      "exe": "svchost.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 10059776,
        "peak_working_set": 10100736,
        "private_usage": 1826816,
        "pagefile_usage": 1826816,
        "peak_pagefile_usage": 2043904,
        "quota_paged": 82136,
        "quota_nonpaged": 9184,
        "page_faults": 2646
      }
    },
    {
      "pid": 5528,
      "exe": "ss_monitor.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 9932800,
        "peak_working_set": 13508608,
        "private_usage": 5697536,
        "pagefile_usage": 5697536,
        "peak_pagefile_usage": 5931008,
        "quota_paged": 114200,
        "quota_nonpaged": 12760,
        "page_faults": 3615
      }
    },
    {
      "pid": 2520,
      "exe": "svchost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 9900032,
        "peak_working_set": 11550720,
        "private_usage": 1921024,
        "pagefile_usage": 1921024,
        "peak_pagefile_usage": 3489792,
        "quota_paged": 85304,
        "quota_nonpaged": 10392,
        "page_faults": 4342
      }
    },
    {
      "pid": 7116,
      "exe": "svchost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 9883648,
        "peak_working_set": 11325440,
        "private_usage": 1818624,
        "pagefile_usage": 1818624,
        "peak_pagefile_usage": 2646016,
        "quota_paged": 84592,
        "quota_nonpaged": 9256,
        "page_faults": 3090
      }
    },
    {
      "pid": 9328,
      "exe": "vmcompute.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 9342976,
        "peak_working_set": 10903552,
        "private_usage": 2093056,
        "pagefile_usage": 2093056,
        "peak_pagefile_usage": 2318336,
        "quota_paged": 72424,
        "quota_nonpaged": 10312,
        "page_faults": 2955
      }
    },
    {
      "pid": 5004,
      "exe": "unsecapp.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 9330688,
        "peak_working_set": 10080256,
        "private_usage": 1761280,
        "pagefile_usage": 1761280,
        "peak_pagefile_usage": 1945600,
        "quota_paged": 73032,
        "quota_nonpaged": 8848,
        "page_faults": 2681
      }
    },
    {
      "pid": 6224,
      "exe": "svchost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 9252864,
        "peak_working_set": 10100736,
        "private_usage": 1683456,
        "pagefile_usage": 1683456,
        "peak_pagefile_usage": 1990656,
        "quota_paged": 72512,
        "quota_nonpaged": 9120,
        "page_faults": 2821
      }
    },
    {
      "pid": 14068,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8925184,
        "peak_working_set": 9891840,
        "private_usage": 3805184,
        "pagefile_usage": 3805184,
        "peak_pagefile_usage": 7380992,
        "quota_paged": 128464,
        "quota_nonpaged": 9392,
        "page_faults": 3158
      }
    },
    {
      "pid": 2600,
      "exe": "amdfendrsr.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8876032,
        "peak_working_set": 10424320,
        "private_usage": 2592768,
        "pagefile_usage": 2592768,
        "peak_pagefile_usage": 2850816,
        "quota_paged": 113768,
        "quota_nonpaged": 10672,
        "page_faults": 2837
      }
    },
    {
      "pid": 4800,
      "exe": "svchost.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8749056,
        "peak_working_set": 9764864,
        "private_usage": 1949696,
        "pagefile_usage": 1949696,
        "peak_pagefile_usage": 2691072,
        "quota_paged": 76264,
        "quota_nonpaged": 9768,
        "page_faults": 2840
      }
    },
    {
      "pid": 1564,
      "exe": "wininit.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8675328,
        "peak_working_set": 9457664,
        "private_usage": 1671168,
        "pagefile_usage": 1671168,
        "peak_pagefile_usage": 2387968,
        "quota_paged": 86168,
        "quota_nonpaged": 12112,
        "page_faults": 3794
      }
    },
    {
      "pid": 2552,
      "exe": "atiesrxx.exe",
      "threads": 7,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8585216,
        "peak_working_set": 9830400,
        "private_usage": 2023424,
        "pagefile_usage": 2023424,
        "peak_pagefile_usage": 2220032,
        "quota_paged": 95560,
        "quota_nonpaged": 11768,
        "page_faults": 2742
      }
    },
    {
      "pid": 10208,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8572928,
        "peak_working_set": 9900032,
        "private_usage": 3813376,
        "pagefile_usage": 3813376,
        "peak_pagefile_usage": 7393280,
        "quota_paged": 128464,
        "quota_nonpaged": 9392,
        "page_faults": 3157
      }
    },
    {
      "pid": 12244,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8380416,
        "peak_working_set": 9973760,
        "private_usage": 3928064,
        "pagefile_usage": 3928064,
        "peak_pagefile_usage": 7405568,
        "quota_paged": 128144,
        "quota_nonpaged": 9392,
        "page_faults": 3143
      }
    },
    {
      "pid": 13132,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8376320,
        "peak_working_set": 9969664,
        "private_usage": 3919872,
        "pagefile_usage": 3919872,
        "peak_pagefile_usage": 7397376,
        "quota_paged": 128144,
        "quota_nonpaged": 9392,
        "page_faults": 3107
      }
    },
    {
      "pid": 13864,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8294400,
        "peak_working_set": 10051584,
        "private_usage": 3854336,
        "pagefile_usage": 3854336,
        "peak_pagefile_usage": 7258112,
        "quota_paged": 128512,
        "quota_nonpaged": 9392,
        "page_faults": 3142
      }
    },
    {
      "pid": 12872,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8282112,
        "peak_working_set": 10055680,
        "private_usage": 4100096,
        "pagefile_usage": 4100096,
        "peak_pagefile_usage": 7266304,
        "quota_paged": 128512,
        "quota_nonpaged": 9392,
        "page_faults": 3069
      }
    },
    {
      "pid": 5480,
      "exe": "ETDService.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 8085504,
        "peak_working_set": 9211904,
        "private_usage": 1613824,
        "pagefile_usage": 1613824,
        "peak_pagefile_usage": 2064384,
        "quota_paged": 88288,
        "quota_nonpaged": 8912,
        "page_faults": 2457
      }
    },
    {
      "pid": 14424,
      "exe": "conhost.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7577600,
        "peak_working_set": 7577600,
        "private_usage": 1241088,
        "pagefile_usage": 1241088,
        "peak_pagefile_usage": 1241088,
        "quota_paged": 115528,
        "quota_nonpaged": 8704,
        "page_faults": 1937
      }
    },
    {
      "pid": 7276,
      "exe": "wlanext.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7327744,
        "peak_working_set": 8159232,
        "private_usage": 1339392,
        "pagefile_usage": 1339392,
        "peak_pagefile_usage": 1527808,
        "quota_paged": 72216,
        "quota_nonpaged": 8704,
        "page_faults": 2177
      }
    },
    {
      "pid": 13084,
      "exe": "conhost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7262208,
        "peak_working_set": 7577600,
        "private_usage": 1220608,
        "pagefile_usage": 1220608,
        "peak_pagefile_usage": 1269760,
        "quota_paged": 114248,
        "quota_nonpaged": 8432,
        "page_faults": 2033
      }
    },
    {
      "pid": 13884,
      "exe": "conhost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7254016,
        "peak_working_set": 7581696,
        "private_usage": 1212416,
        "pagefile_usage": 1212416,
        "peak_pagefile_usage": 1257472,
        "quota_paged": 116808,
        "quota_nonpaged": 8432,
        "page_faults": 2036
      }
    },
    {
      "pid": 4648,
      "exe": "conhost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7237632,
        "peak_working_set": 7589888,
        "private_usage": 1220608,
        "pagefile_usage": 1220608,
        "peak_pagefile_usage": 1310720,
        "quota_paged": 116808,
        "quota_nonpaged": 8432,
        "page_faults": 2026
      }
    },
    {
      "pid": 14096,
      "exe": "conhost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7233536,
        "peak_working_set": 7585792,
        "private_usage": 1224704,
        "pagefile_usage": 1224704,
        "peak_pagefile_usage": 1282048,
        "quota_paged": 114248,
        "quota_nonpaged": 8432,
        "page_faults": 2034
      }
    },
    {
      "pid": 3960,
      "exe": "ServiceCoordinator.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 7184384,
        "peak_working_set": 7532544,
        "private_usage": 1523712,
        "pagefile_usage": 1523712,
        "peak_pagefile_usage": 1601536,
        "quota_paged": 75752,
        "quota_nonpaged": 9184,
        "page_faults": 2158
      }
    },
    {
      "pid": 13248,
      "exe": "cmd.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 6942720,
        "peak_working_set": 6942720,
        "private_usage": 6193152,
        "pagefile_usage": 6193152,
        "peak_pagefile_usage": 6307840,
        "quota_paged": 61280,
        "quota_nonpaged": 6264,
        "page_faults": 1850
      }
    },
    {
      "pid": 4852,
      "exe": "RamStasis.exe",
      "threads": 4,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 6717440,
        "peak_working_set": 7077888,
        "private_usage": 1323008,
        "pagefile_usage": 1323008,
        "peak_pagefile_usage": 1617920,
        "quota_paged": 125136,
        "quota_nonpaged": 7488,
        "page_faults": 715721
      }
    },
    {
      "pid": 6448,
      "exe": "conhost.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 6606848,
        "peak_working_set": 7196672,
        "private_usage": 1277952,
        "pagefile_usage": 1277952,
        "peak_pagefile_usage": 1327104,
        "quota_paged": 113304,
        "quota_nonpaged": 8160,
        "page_faults": 1916
      }
    },
    {
      "pid": 1576,
      "exe": "csrss.exe",
      "threads": 17,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 6553600,
        "peak_working_set": 16494592,
        "private_usage": 3653632,
        "pagefile_usage": 3653632,
        "peak_pagefile_usage": 3973120,
        "quota_paged": 195216,
        "quota_nonpaged": 27080,
        "page_faults": 114375
      }
    },
    {
      "pid": 12816,
      "exe": "cmd.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 5955584,
        "peak_working_set": 7467008,
        "private_usage": 3403776,
        "pagefile_usage": 3403776,
        "peak_pagefile_usage": 6569984,
        "quota_paged": 51432,
        "quota_nonpaged": 6536,
        "page_faults": 2006
      }
    },
    {
      "pid": 11620,
      "exe": "DefenderSessionHelper.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 5660672,
        "peak_working_set": 6053888,
        "private_usage": 1179648,
        "pagefile_usage": 1179648,
        "peak_pagefile_usage": 1306624,
        "quota_paged": 96272,
        "quota_nonpaged": 6672,
        "page_faults": 1649
      }
    },
    {
      "pid": 1360,
      "exe": "csrss.exe",
      "threads": 13,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 5517312,
        "peak_working_set": 7700480,
        "private_usage": 2457600,
        "pagefile_usage": 2457600,
        "peak_pagefile_usage": 3223552,
        "quota_paged": 255184,
        "quota_nonpaged": 21960,
        "page_faults": 3560
      }
    },
    {
      "pid": 12236,
      "exe": "memreduct.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 5447680,
        "peak_working_set": 17551360,
        "private_usage": 2912256,
        "pagefile_usage": 2912256,
        "peak_pagefile_usage": 3174400,
        "quota_paged": 222696,
        "quota_nonpaged": 11568,
        "page_faults": 8181
      }
    },
    {
      "pid": 1888,
      "exe": "fontdrvhost.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 5390336,
        "peak_working_set": 6414336,
        "private_usage": 2146304,
        "pagefile_usage": 2146304,
        "peak_pagefile_usage": 2183168,
        "quota_paged": 55552,
        "quota_nonpaged": 9120,
        "page_faults": 2506
      }
    },
    {
      "pid": 1716,
      "exe": "LsaIso.exe",
      "threads": 1,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 3780608,
        "peak_working_set": 4255744,
        "private_usage": 1212416,
        "pagefile_usage": 1212416,
        "peak_pagefile_usage": 1294336,
        "quota_paged": 32632,
        "quota_nonpaged": 5312,
        "page_faults": 1358
      }
    },
    {
      "pid": 1896,
      "exe": "fontdrvhost.exe",
      "threads": 5,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 3682304,
        "peak_working_set": 4952064,
        "private_usage": 1515520,
        "pagefile_usage": 1515520,
        "peak_pagefile_usage": 1630208,
        "quota_paged": 44240,
        "quota_nonpaged": 7352,
        "page_faults": 1424
      }
    },
    {
      "pid": 9792,
      "exe": "Notepad.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 2019328,
        "peak_working_set": 17674240,
        "private_usage": 3194880,
        "pagefile_usage": 3194880,
        "peak_pagefile_usage": 3317760,
        "quota_paged": 219520,
        "quota_nonpaged": 13336,
        "page_faults": 5059
      }
    },
    {
      "pid": 768,
      "exe": "smss.exe",
      "threads": 2,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 1368064,
        "peak_working_set": 1814528,
        "private_usage": 1200128,
        "pagefile_usage": 1200128,
        "peak_pagefile_usage": 1331200,
        "quota_paged": 15584,
        "quota_nonpaged": 4464,
        "page_faults": 1130
      }
    },
    {
      "pid": 9140,
      "exe": "BraveCrashHandler.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 372736,
        "peak_working_set": 10153984,
        "private_usage": 1843200,
        "pagefile_usage": 1843200,
        "peak_pagefile_usage": 2125824,
        "quota_paged": 111032,
        "quota_nonpaged": 12280,
        "page_faults": 4213
      }
    },
    {
      "pid": 9172,
      "exe": "BraveCrashHandler64.exe",
      "threads": 3,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 323584,
        "peak_working_set": 10145792,
        "private_usage": 1835008,
        "pagefile_usage": 1835008,
        "peak_pagefile_usage": 1957888,
        "quota_paged": 120320,
        "quota_nonpaged": 9768,
        "page_faults": 3969
      }
    },
    {
      "pid": 4,
      "exe": "System",
      "threads": 354,
      "threads_suspended": 0,
      "suspend_state": "running",
      "memory": {
        "ok": true,
        "working_set": 151552,
        "peak_working_set": 647168,
        "private_usage": 45056,
        "pagefile_usage": 45056,
        "peak_pagefile_usage": 69632,
        "quota_paged": 0,
        "quota_nonpaged": 272,
        "page_faults": 1646
      }
    },
    {
      "pid": 188,
      "exe": "Secure System",
      "threads": 0,
      "threads_suspended": 0,
      "suspend_state": "?",
      "memory": {
        "ok": true,
        "working_set": 114688,
        "peak_working_set": 135168,
        "private_usage": 180224,
        "pagefile_usage": 180224,
        "peak_pagefile_usage": 229376,
        "quota_paged": 9656,
        "quota_nonpaged": 272,
        "page_faults": 65
      }
    },
    {
      "pid": 0,
      "exe": "[System Process]",
      "threads": 36,
      "threads_suspended": 0,
      "suspend_state": "denied",
      "memory": {
        "ok": false,
        "win32_error": 0
      },
      "open_error": 87
    }
  ]
}
`


## `RamStasis.exe guard --json`
`json
{
  "command": "guard",
  "scan_ok": true,
  "processes_scanned": 157,
  "kernel_drivers_scanned": 225,
  "modules_and_drivers_scanned": 7851,
  "hits": 0,
  "components": [],
  "blocked": false
}
`


## `RamStasis.exe tree 2588 --json`
`json
{
  "command": "tree",
  "root_pid": 2588,
  "root_exe": "Notepad.exe",
  "process_count": 1,
  "processes": [
    {
      "pid": 2588,
      "depth": 0,
      "exe": "Notepad.exe",
      "image": "C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_11.2607.14.0_x64__8wekyb3d8bbwe\\Notepad\\Notepad.exe",
      "create_time": 134370549999470685,
      "threads": 79,
      "state": "running",
      "memory": {
        "ok": true,
        "working_set": 41836544,
        "peak_working_set": 202182656,
        "private_usage": 144457728,
        "pagefile_usage": 144457728,
        "peak_pagefile_usage": 150691840,
        "quota_paged": 1118016,
        "quota_nonpaged": 87464,
        "page_faults": 80104
      }
    }
  ],
  "total": {
    "working_set": 41836544,
    "commit": 144457728,
    "threads": 79
  }
}
`


## `RamStasis.exe freeze 2588 --json`
`json
{
  "command": "freeze",
  "root_pid": 2588,
  "exe": "Notepad.exe",
  "image": "C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_11.2607.14.0_x64__8wekyb3d8bbwe\\Notepad\\Notepad.exe",
  "tree": true,
  "process_count": 1,
  "system_before": {
    "ok": true,
    "commit_total": 8797253632,
    "commit_limit": 33509728256,
    "free_commit": 24712474624,
    "commit_peak": 11807313920,
    "physical_total": 7739924480,
    "physical_available": 2549354496,
    "system_cache": 1036259328,
    "kernel_total": 800677888,
    "kernel_paged": 299061248,
    "kernel_nonpaged": 501616640,
    "page_size": 4096,
    "handle_count": 85709,
    "process_count": 157,
    "thread_count": 2266
  },
  "pagefile_before": {
    "ok": true,
    "win32_error": 0,
    "file_count": 1,
    "total_size_pages": 6291456,
    "total_in_use_pages": 55549,
    "total_size_bytes": 25769803776,
    "total_in_use_bytes": 227528704,
    "peak_usage_bytes": 379850752
  },
  "tree_commit_before": 144457728,
  "windows": {
    "count": 27,
    "minimized": 5,
    "list": [
      {
        "pid": 2588,
        "hwnd": 525690,
        "visible_before": true,
        "iconic_before": false,
        "minimized_by_us": true,
        "iconic_after": true,
        "hung_after": false,
        "left": 192,
        "top": 192,
        "right": 1344,
        "bottom": 784,
        "title": "Untitled - Notepad"
      },
      {
        "pid": 2588,
        "hwnd": 328008,
        "visible_before": true,
        "iconic_before": false,
        "minimized_by_us": true,
        "iconic_after": true,
        "hung_after": false,
        "left": 224,
        "top": 224,
        "right": 1376,
        "bottom": 816,
        "title": "Untitled - Notepad"
      },
      {
        "pid": 2588,
        "hwnd": 198046,
        "visible_before": true,
        "iconic_before": false,
        "minimized_by_us": true,
        "iconic_after": true,
        "hung_after": false,
        "left": 160,
        "top": 160,
        "right": 1312,
        "bottom": 752,
        "title": "Untitled - Notepad"
      },
      {
        "pid": 2588,
        "hwnd": 394652,
        "visible_before": true,
        "iconic_before": false,
        "minimized_by_us": true,
        "iconic_after": true,
        "hung_after": false,
        "left": 192,
        "top": 192,
        "right": 1344,
        "bottom": 784,
        "title": "*nudes.txt - Notepad"
      },
      {
        "pid": 2588,
        "hwnd": 329102,
        "visible_before": true,
        "iconic_before": false,
        "minimized_by_us": true,
        "iconic_after": true,
        "hung_after": false,
        "left": 224,
        "top": 224,
        "right": 1376,
        "bottom": 816,
        "title": "Untitled - Notepad"
      },
      {
        "pid": 2588,
        "hwnd": 328346,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 1,
        "bottom": 1,
        "title": "GDI+ Window (Notepad.exe)"
      },
      {
        "pid": 2588,
        "hwnd": 918446,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 263750,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 722112,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 394664,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 853194,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 722362,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 2621500,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 132662,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 459752,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 132544,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 132596,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 132664,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 1901806,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 8324540,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 198060,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 132604,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 132550,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 198114,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 198048,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      },
      {
        "pid": 2588,
        "hwnd": 655716,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "MSCTFIME UI"
      },
      {
        "pid": 2588,
        "hwnd": 2163894,
        "visible_before": false,
        "iconic_before": false,
        "minimized_by_us": false,
        "iconic_after": false,
        "hung_after": false,
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0,
        "title": "Default IME"
      }
    ]
  },
  "processes": [
    {
      "pid": 2588,
      "depth": 0,
      "exe": "Notepad.exe",
      "opened": true,
      "late_child": false,
      "before": {
        "ok": true,
        "working_set": 41836544,
        "peak_working_set": 202182656,
        "private_usage": 144457728,
        "pagefile_usage": 144457728,
        "peak_pagefile_usage": 150691840,
        "quota_paged": 1118016,
        "quota_nonpaged": 87464,
        "page_faults": 80104
      },
      "after": {
        "ok": true,
        "working_set": 0,
        "peak_working_set": 202182656,
        "private_usage": 145219584,
        "pagefile_usage": 145219584,
        "peak_pagefile_usage": 150691840,
        "quota_paged": 1112480,
        "quota_nonpaged": 87464,
        "page_faults": 81084
      },
      "trim_verified": true,
      "empty_working_set_ret": 1,
      "empty_working_set_win32": 0,
      "suspend": {
        "nt_suspend_process": "STATUS_SUCCESS",
        "ntstatus": 0,
        "threads_seen": 79,
        "threads_incremented": 79,
        "threads_escaped": 0,
        "threads_individually_suspended": 0
      }
    }
  ],
  "system_after": {
    "ok": true,
    "commit_total": 8805535744,
    "commit_limit": 33509728256,
    "free_commit": 24704192512,
    "commit_peak": 11807313920,
    "physical_total": 7739924480,
    "physical_available": 2546843648,
    "system_cache": 1044836352,
    "kernel_total": 800231424,
    "kernel_paged": 298786816,
    "kernel_nonpaged": 501444608,
    "page_size": 4096,
    "handle_count": 85655,
    "process_count": 157,
    "thread_count": 2266
  },
  "pagefile_after": {
    "ok": true,
    "win32_error": 0,
    "file_count": 1,
    "total_size_pages": 6291456,
    "total_in_use_pages": 55549,
    "total_size_bytes": 25769803776,
    "total_in_use_bytes": 227528704,
    "peak_usage_bytes": 379850752
  },
  "delta": {
    "working_set": -41836544,
    "commit_total": 8282112,
    "free_commit_after": 24704192512
  },
  "state": {
    "written": true,
    "path": "state\\frz-2588.state",
    "processes": 1,
    "windows": 27
  },
  "ok": true
}
`


## `RamStasis.exe thaw 2588 --json`
`json
{
  "command": "thaw",
  "root_pid": 2588,
  "state_file_found": true,
  "state_file": "state\\frz-2588.state",
  "state_format": 2,
  "processes_resumed": 1,
  "processes_skipped": 0,
  "threads_verified": 79,
  "thread_count_mismatches": 0,
  "message_ok": true,
  "wake_latency_ms": 0.0,
  "cpu_latency_ms": 0.0,
  "window_notes": [
    "hwnd 0x0008057A: ShowWindowAsync(SW_RESTORE) -> 1 win32=0",
    "hwnd 0x00050148: ShowWindowAsync(SW_RESTORE) -> 1 win32=0",
    "hwnd 0x0003059E: ShowWindowAsync(SW_RESTORE) -> 1 win32=0",
    "hwnd 0x0006059C: ShowWindowAsync(SW_RESTORE) -> 1 win32=0",
    "hwnd 0x0005058E: ShowWindowAsync(SW_RESTORE) -> 1 win32=0",
    "hwnd 0x0005029A: not visible before the freeze - left untouched",
    "hwnd 0x000E03AE: not visible before the freeze - left untouched",
    "hwnd 0x00040646: not visible before the freeze - left untouched",
    "hwnd 0x000B04C0: not visible before the freeze - left untouched",
    "hwnd 0x000605A8: not visible before the freeze - left untouched",
    "hwnd 0x000D04CA: not visible before the freeze - left untouched",
    "hwnd 0x000B05BA: not visible before the freeze - left untouched",
    "hwnd 0x0028003C: not visible before the freeze - left untouched",
    "hwnd 0x00020636: not visible before the freeze - left untouched",
    "hwnd 0x000703E8: not visible before the freeze - left untouched",
    "hwnd 0x000205C0: not visible before the freeze - left untouched",
    "hwnd 0x000205F4: not visible before the freeze - left untouched",
    "hwnd 0x00020638: not visible before the freeze - left untouched",
    "hwnd 0x001D04EE: not visible before the freeze - left untouched",
    "hwnd 0x007F05BC: not visible before the freeze - left untouched",
    "hwnd 0x000305AC: not visible before the freeze - left untouched",
    "hwnd 0x000205FC: not visible before the freeze - left untouched",
    "hwnd 0x000205C6: not visible before the freeze - left untouched",
    "hwnd 0x000305E2: not visible before the freeze - left untouched",
    "hwnd 0x000305A0: not visible before the freeze - left untouched",
    "hwnd 0x000A0164: not visible before the freeze - left untouched",
    "hwnd 0x002104B6: not visible before the freeze - left untouched"
  ],
  "ok": true
}
`


## `RamStasis.exe stats 2588 --json`
`json
{
  "command": "stats",
  "pid": 2588,
  "exe": "Notepad.exe",
  "image": "C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_11.2607.14.0_x64__8wekyb3d8bbwe\\Notepad\\Notepad.exe",
  "create_time": 134370549999470685,
  "thread_count": 79,
  "threads_with_suspend_count_gt0": 0,
  "threads_unqueryable": 0,
  "window_count": 27,
  "process": {
    "ok": true,
    "working_set": 24621056,
    "peak_working_set": 202182656,
    "private_usage": 146927616,
    "pagefile_usage": 146927616,
    "peak_pagefile_usage": 150691840,
    "quota_paged": 1112992,
    "quota_nonpaged": 87464,
    "page_faults": 87222
  },
  "system": {
    "ok": true,
    "commit_total": 8843436032,
    "commit_limit": 33509728256,
    "free_commit": 24666292224,
    "commit_peak": 11807313920,
    "physical_total": 7739924480,
    "physical_available": 2547265536,
    "system_cache": 1039884288,
    "kernel_total": 801681408,
    "kernel_paged": 299560960,
    "kernel_nonpaged": 502120448,
    "page_size": 4096,
    "handle_count": 85662,
    "process_count": 157,
    "thread_count": 2266
  },
  "pagefile": {
    "ok": true,
    "win32_error": 0,
    "file_count": 1,
    "total_size_pages": 6291456,
    "total_in_use_pages": 55549,
    "total_size_bytes": 25769803776,
    "total_in_use_bytes": 227528704,
    "peak_usage_bytes": 379850752
  },
  "free_commit": 24666292224,
  "threads": [
    {
      "tid": 11328,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 625000,
      "user_100ns": 156250,
      "start_address_ok": true,
      "start_address": "0x7FF70BFF4790"
    },
    {
      "tid": 7544,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC75FA2E0"
    },
    {
      "tid": 12096,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 781250,
      "user_100ns": 1718750,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 14296,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEA91B55D0"
    },
    {
      "tid": 13104,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC5985670"
    },
    {
      "tid": 3008,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC75FA2E0"
    },
    {
      "tid": 6860,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE410AFB70"
    },
    {
      "tid": 9944,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEA1717D40"
    },
    {
      "tid": 7936,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEA7BF77A0"
    },
    {
      "tid": 14564,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 15524,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 156250,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 10772,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 2776,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 13280,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 625000,
      "user_100ns": 1875000,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 13568,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 1250000,
      "user_100ns": 937500,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 4660,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 468750,
      "user_100ns": 1406250,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 8920,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE410AFB70"
    },
    {
      "tid": 14568,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE410AFB70"
    },
    {
      "tid": 9596,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE410AFB70"
    },
    {
      "tid": 6916,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 16156,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 10096,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 1304,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 14828,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 11528,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 15840,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 4616,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 7896,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 13240,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 11868,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 10084,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 2788,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 5644,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 3125000,
      "user_100ns": 3125000,
      "start_address_ok": true,
      "start_address": "0x7FFE550816F0"
    },
    {
      "tid": 13480,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE55170090"
    },
    {
      "tid": 15560,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 156250,
      "user_100ns": 156250,
      "start_address_ok": true,
      "start_address": "0x7FFE551AD370"
    },
    {
      "tid": 4452,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE551A1670"
    },
    {
      "tid": 8676,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 7400,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 4340,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 4644,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 3480,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 5936,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 15836,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 14116,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 11052,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 3284,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 4400,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 7704,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 1372,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 13268,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 16208,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 625000,
      "user_100ns": 625000,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 14164,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 312500,
      "user_100ns": 312500,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 1984,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 3936,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 625000,
      "user_100ns": 1875000,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 12824,
      "suspend_count": 0,
      "priority": 2,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE935D0D80"
    },
    {
      "tid": 11932,
      "suspend_count": 0,
      "priority": 2,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE935D0D80"
    },
    {
      "tid": 12180,
      "suspend_count": 0,
      "priority": 2,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE935D0D80"
    },
    {
      "tid": 10088,
      "suspend_count": 0,
      "priority": 2,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE935D0D80"
    },
    {
      "tid": 13028,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE88530760"
    },
    {
      "tid": 14864,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 16160,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 8404,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 15772,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 4088,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 5048,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 132,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 1292,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 1112,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 1156,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 7296,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 7412,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 15752,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB77D5810"
    },
    {
      "tid": 5624,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 468750,
      "user_100ns": 1250000,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 13996,
      "suspend_count": 0,
      "priority": 15,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE410AFB70"
    },
    {
      "tid": 2372,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 7020,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 156250,
      "start_address_ok": true,
      "start_address": "0x7FFEC504CD00"
    },
    {
      "tid": 3248,
      "suspend_count": 0,
      "priority": 2,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFE935D0D80"
    },
    {
      "tid": 2740,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEB5915C90"
    },
    {
      "tid": 14224,
      "suspend_count": 0,
      "priority": 0,
      "kernel_100ns": 0,
      "user_100ns": 0,
      "start_address_ok": true,
      "start_address": "0x7FFEC75FA2E0"
    }
  ]
}
`

## WHAT THE OVERLAY CARD NEEDS

The card needs, per frozen app: the app name/icon, the RAM that was freed, the commit it still
holds, and the screen rect to draw at. All of it is in the freeze/thaw JSON:

- `freeze` -> `windows[]` : per top-level window of the frozen tree - hwnd, rect, class, title,
  was_visible, was_iconic, minimized_by_us. **The card is drawn at the rect of the window the
  user was actually looking at** (the visible, non-iconic one).
- `freeze` -> `delta` : working_set_freed, commit_delta (always ~0 - commit is a promise, not
  residency), pagefile deltas.
- `freeze` -> `tree_commit_before` / `system_before.free_commit` : the commit the app still holds
  and the system's free commit (for the <15% warning row).
- `freeze` -> `state` : the state file path + format, so the UI knows a thaw is pending.
- `thaw`   -> `wake_latency_ms`, `cpu_latency_ms`, `window_notes[]` (why a window was or was not
  restored).

## CAPABILITY PROBE (do it once at startup)

Run `RamStasis.exe list --json` and check:
- `processes[].suspend_state` == "running" -> the tree/guard generation is present.
- `RamStasis.exe guard --json` -> `command` == "guard" and the `components` key exists -> guard present.
If `guard` is missing, the engine is the OLD single-process generation: grey out the guard row and
the tree row rather than pretending they work.