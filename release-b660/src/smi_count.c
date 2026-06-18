#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "Api.h"

/*
 * MSR_SMI_COUNT = 0x34 on Intel (architectural since Nehalem).
 * Counts SMIs handled since power-on. Per-core, but generally identical
 * across cores since SMI is broadcast.
 *
 * Methodology:
 *  1. Read MSR 0x34 (each read = 1 SMI itself, contributes +1 to count)
 *  2. Wait N seconds idle
 *  3. Read again, delta = idle SMI rate + N rdmsr SMIs
 *  4. Do M test operations
 *  5. Read again, delta = M test SMIs + 1 rdmsr SMI + small idle window
 *
 * Note: every CMD_RDMSR call IS itself one SMI, so each read adds +1.
 */

#define MSR_SMI_COUNT 0x34U

int main(void) {
    uint64_t a, b, c, d;
    PROCESS_INFO p;

    printf("=== SMI count detection measurement ===\n");
    if (!Init() || !Ping()) { printf("init failed\n"); return 1; }

    printf("\n--- baseline: MSR_SMI_COUNT (0x34) ---\n");
    if (!RdMsr(MSR_SMI_COUNT, &a)) { printf("RdMsr failed (firmware lacks CMD_RDMSR? need reflash)\n"); Close(); return 1; }
    printf("  reading 1: 0x%016llX = %llu\n", (unsigned long long)a, (unsigned long long)a);

    /* Two rapid reads to measure rdmsr's own SMI cost */
    if (!RdMsr(MSR_SMI_COUNT, &b)) { printf("RdMsr failed\n"); Close(); return 1; }
    printf("  reading 2: 0x%016llX = %llu  (delta=%llu)\n",
           (unsigned long long)b, (unsigned long long)b, (unsigned long long)(b - a));
    printf("  -> each RdMsr SMI itself accounts for the delta above\n");

    /* === Idle window === */
    printf("\n--- idle window: 5 seconds of no SmmMem activity ---\n");
    if (!RdMsr(MSR_SMI_COUNT, &a)) { Close(); return 1; }
    printf("  before idle: %llu\n", (unsigned long long)a);
    Sleep(5000);
    if (!RdMsr(MSR_SMI_COUNT, &b)) { Close(); return 1; }
    printf("  after  idle: %llu  (delta=%llu over 5s)\n",
           (unsigned long long)b, (unsigned long long)(b - a));
    /* subtract 1 for the rdmsr itself */
    unsigned long long idle_smis = (b - a) > 0 ? (b - a) - 1 : 0;
    double idle_rate = idle_smis / 5.0;
    printf("  -> background SMI rate ~%.2f SMIs/sec (excluding our rdmsr)\n", idle_rate);

    /* === Workload: 100 ReadVirts === */
    printf("\n--- workload: 100x ReadVirt on explorer.exe ---\n");
    if (!FindProcessByName("explorer.exe", &p)) {
        printf("  explorer not found, skip\n");
    } else {
        unsigned char buf[64];
        if (!RdMsr(MSR_SMI_COUNT, &c)) { Close(); return 1; }
        for (int i = 0; i < 100; i++) {
            ReadVirt(p.Pid, p.ImageBase, buf, sizeof(buf));
        }
        if (!RdMsr(MSR_SMI_COUNT, &d)) { Close(); return 1; }
        unsigned long long total = d - c;
        printf("  before: %llu\n", (unsigned long long)c);
        printf("  after:  %llu  (delta=%llu)\n",
               (unsigned long long)d, total);
        printf("  -> 100 ReadVirts + 1 rdmsr + small idle window\n");
        printf("  -> attributable to ReadVirt: ~%llu SMIs (expected: 100 if isolated)\n",
               total > 1 ? total - 1 : 0);
    }

    /* === Workload: 1000 Pings (smallest possible op) === */
    printf("\n--- workload: 1000x Ping (minimum-cost op) ---\n");
    if (!RdMsr(MSR_SMI_COUNT, &c)) { Close(); return 1; }
    for (int i = 0; i < 1000; i++) Ping();
    if (!RdMsr(MSR_SMI_COUNT, &d)) { Close(); return 1; }
    printf("  delta = %llu (expected 1001: 1000 pings + 1 rdmsr)\n",
           (unsigned long long)(d - c));

    /* === Detection analysis === */
    printf("\n--- detection analysis ---\n");
    printf("  idle background:     %.2f SMIs/sec\n", idle_rate);
    printf("  cheat hot-loop @60Hz: +60 SMIs/sec (1 read per frame)\n");
    printf("  cheat full ESP @60Hz: +~600 SMIs/sec (10 reads per frame, no batching)\n");
    if (idle_rate < 5) {
        printf("  -> idle is QUIET. AC sampling MSR 0x34 would easily detect even 1 cheat op/sec.\n");
    } else if (idle_rate < 50) {
        printf("  -> idle moderate. Cheat needs batching to stay near noise floor.\n");
    } else {
        printf("  -> idle is noisy. Cheat <100 SMIs/sec may hide. Still detectable in stats.\n");
    }

    Close();
    printf("\n=== done ===\n");
    return 0;
}
