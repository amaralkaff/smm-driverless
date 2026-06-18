# SmmMem — B660 GAMING X

Driverless Windows memory access via SMM. Forked from [vtilo's release](https://www.unknowncheats.me/forum/anti-cheat-bypass/754948-smmmem-driverless-windows-memory-access-smm.html).

**Works on:** Gigabyte B660 GAMING X DDR4 rev 1.0 + i5-12400F + BIOS F35a.

## Build (x64 VS Developer Cmd)

```
cd src
build.cmd
```

## Flash

Patch stock BIOS with [UEFITool 0.28.0](https://github.com/LongSoft/UEFITool/releases/tag/0.28.0):

```
UEFIReplace stock.bin 04EAAAA1-29A1-11D7-8838-00500473D4EB 10 Smm.efi -o tmp.bin -all
UEFIReplace tmp.bin   ECEBCB00-D9C8-11E4-AF3D-8CDCD426C973 10 Dxe.efi -o modded.bin -all
```

Rename `modded.bin` → `gigabyte.bin`, flash via **Q-Flash Plus rear button** (not menu Q-Flash — rejects unsigned).

## Run

```
Client.exe          # ping
test.exe            # API smoke test
stress_test.exe     # perf + write round-trip
smi_count.exe       # MSR 0x34 detection measurement
```

## Notes

- Adds `CMD_RDMSR` / `CMD_WRMSR` beyond vtilo's LATEST.
- ~0.44 ms per SMI, ~800 KB/s read ceiling. Batch hard.
- Idle SMI rate ~0/sec on this board → any cheat use is binary-detectable via MSR 0x34. Don't run against AC that samples this MSR.
