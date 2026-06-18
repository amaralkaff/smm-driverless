#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Api.h"

#define MSR_SMI_COUNT 0x34U

static double ms(LARGE_INTEGER s, LARGE_INTEGER e, LARGE_INTEGER f) {
    return (double)(e.QuadPart - s.QuadPart) * 1000.0 / (double)f.QuadPart;
}

int main(void) {
    PROCESS_INFO exp;
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    uint64_t smi_a, smi_b;

    printf("=== ReadVirtBatch perf vs serial reads ===\n");
    if (!Init() || !Ping()) { printf("init failed\n"); return 1; }

    if (!FindProcessByName("explorer.exe", &exp)) { printf("no explorer\n"); Close(); return 1; }
    printf("target: explorer pid=%u base=0x%016llX\n", exp.Pid, (unsigned long long)exp.ImageBase);

    /* Build 32 scattered 64-byte read items at offsets 0, 0x100, 0x200, ... */
    const uint32_t N = 32;
    BATCH_ITEM items[64];
    unsigned char *bufs[64];
    void *buf_ptrs[64];
    static unsigned char heap[64 * 64];
    for (uint32_t i = 0; i < N; i++) {
        items[i].Pid = exp.Pid;
        items[i].Size = 64;
        items[i].Va = exp.ImageBase + (uint64_t)i * 0x100;
        bufs[i] = heap + i * 64;
        buf_ptrs[i] = bufs[i];
        memset(bufs[i], 0, 64);
    }

    /* === Serial: N separate ReadVirt calls === */
    printf("\n--- serial: %u x ReadVirt(64B) ---\n", N);
    RdMsr(MSR_SMI_COUNT, &smi_a);
    QueryPerformanceCounter(&t0);
    int s_ok = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (ReadVirt(items[i].Pid, items[i].Va, bufs[i], items[i].Size)) s_ok++;
    }
    QueryPerformanceCounter(&t1);
    RdMsr(MSR_SMI_COUNT, &smi_b);
    double s_total = ms(t0, t1, freq);
    uint64_t s_smis = (smi_b - smi_a) - 1;  /* minus 1 for the final rdmsr itself */
    printf("  ok=%d/%u  total=%.2f ms  per_op=%.3f ms  SMIs=%llu (expected %u)\n",
           s_ok, N, s_total, s_total / N,
           (unsigned long long)s_smis, N);

    /* === Batch: 1 single SMI returns all N === */
    printf("\n--- batch: 1 x ReadVirtBatch(%u items @ 64B) ---\n", N);
    for (uint32_t i = 0; i < N; i++) memset(bufs[i], 0, 64);
    RdMsr(MSR_SMI_COUNT, &smi_a);
    QueryPerformanceCounter(&t0);
    int b_ok = ReadVirtBatch(items, buf_ptrs, N);
    QueryPerformanceCounter(&t1);
    RdMsr(MSR_SMI_COUNT, &smi_b);
    double b_total = ms(t0, t1, freq);
    uint64_t b_smis = (smi_b - smi_a) - 1;
    printf("  ok=%d/%u  total=%.2f ms  SMIs=%llu (expected 1)\n",
           b_ok, N, b_total, (unsigned long long)b_smis);

    /* === Summary === */
    printf("\n--- summary ---\n");
    printf("  serial: %.2f ms, %llu SMIs\n", s_total, (unsigned long long)s_smis);
    printf("  batch:  %.2f ms, %llu SMIs\n", b_total, (unsigned long long)b_smis);
    if (b_total > 0)
        printf("  speedup: %.1fx faster\n", s_total / b_total);
    if (b_smis > 0)
        printf("  SMI reduction: %.1fx fewer SMIs (detection delta)\n",
               (double)s_smis / (double)b_smis);

    /* Sanity: first item from batch should match a separate ReadVirt */
    unsigned char verify[64];
    if (ReadVirt(items[0].Pid, items[0].Va, verify, 64)) {
        int match = memcmp(verify, bufs[0], 64) == 0;
        printf("\n  data integrity (item 0 vs ReadVirt): %s\n", match ? "MATCH" : "MISMATCH");
        if (!match) {
            printf("    batch[0]:  "); for (int i=0;i<16;i++) printf("%02X ",bufs[0][i]); printf("\n");
            printf("    ReadVirt:  "); for (int i=0;i<16;i++) printf("%02X ",verify[i]); printf("\n");
        }
    }

    Close();
    printf("\n=== done ===\n");
    return 0;
}
