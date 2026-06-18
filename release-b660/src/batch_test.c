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

    printf("=== ReadVirtBatch / ReadVirtBatchH perf comparison ===\n");
    if (!Init() || !Ping()) { printf("init failed\n"); return 1; }

    if (!FindProcessByName("explorer.exe", &exp)) { printf("no explorer\n"); Close(); return 1; }
    printf("target: explorer pid=%u base=0x%016llX\n",
           exp.Pid, (unsigned long long)exp.ImageBase);

    const uint32_t N = 32;
    BATCH_ITEM items_pid[64], items_h[64];
    unsigned char *bufs[64];
    void *buf_ptrs[64];
    static unsigned char heap[64 * 64];

    for (uint32_t i = 0; i < N; i++) {
        items_pid[i].Pid = exp.Pid;
        items_pid[i].Size = 64;
        items_pid[i].Va = exp.ImageBase + (uint64_t)i * 0x100;
        bufs[i] = heap + i * 64;
        buf_ptrs[i] = bufs[i];
    }

    /* === Serial: N separate ReadVirt calls === */
    printf("\n--- serial: %u x ReadVirt(64B) ---\n", N);
    for (uint32_t i = 0; i < N; i++) memset(bufs[i], 0, 64);
    RdMsr(MSR_SMI_COUNT, &smi_a);
    QueryPerformanceCounter(&t0);
    int s_ok = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (ReadVirt(items_pid[i].Pid, items_pid[i].Va, bufs[i], items_pid[i].Size)) s_ok++;
    }
    QueryPerformanceCounter(&t1);
    RdMsr(MSR_SMI_COUNT, &smi_b);
    double s_total = ms(t0, t1, freq);
    uint64_t s_smis = (smi_b - smi_a) - 1;
    printf("  ok=%d/%u  total=%.2f ms  per_op=%.3f ms  SMIs=%llu\n",
           s_ok, N, s_total, s_total / N, (unsigned long long)s_smis);

    /* === Batch (PID-keyed, group-by-pid cache) === */
    printf("\n--- batch (pid-keyed, group-by-pid cache): 1 SMI for %u items ---\n", N);
    for (uint32_t i = 0; i < N; i++) memset(bufs[i], 0, 64);
    RdMsr(MSR_SMI_COUNT, &smi_a);
    QueryPerformanceCounter(&t0);
    int b_ok = ReadVirtBatch(items_pid, buf_ptrs, N);
    QueryPerformanceCounter(&t1);
    RdMsr(MSR_SMI_COUNT, &smi_b);
    double b_total = ms(t0, t1, freq);
    uint64_t b_smis = (smi_b - smi_a) - 1;
    printf("  ok=%d/%u  total=%.2f ms  SMIs=%llu\n",
           b_ok, N, b_total, (unsigned long long)b_smis);

    /* === Batch_H (process handle, no lookup per item) === */
    printf("\n--- batch_h (process handle cached, zero lookups per item) ---\n");
    uint32_t handle = 0;
    if (!SmmOpenProcess(exp.Pid, &handle)) {
        printf("  OpenProcess FAILED\n"); Close(); return 1;
    }
    printf("  handle = %u (slot %u of %u)\n", handle, handle, 16);
    for (uint32_t i = 0; i < N; i++) {
        items_h[i].Pid = handle;     /* Pid field holds Handle */
        items_h[i].Size = 64;
        items_h[i].Va = items_pid[i].Va;
        memset(bufs[i], 0, 64);
    }
    RdMsr(MSR_SMI_COUNT, &smi_a);
    QueryPerformanceCounter(&t0);
    int h_ok = ReadVirtBatchH(items_h, buf_ptrs, N);
    QueryPerformanceCounter(&t1);
    RdMsr(MSR_SMI_COUNT, &smi_b);
    double h_total = ms(t0, t1, freq);
    uint64_t h_smis = (smi_b - smi_a) - 1;
    printf("  ok=%d/%u  total=%.2f ms  SMIs=%llu\n",
           h_ok, N, h_total, (unsigned long long)h_smis);

    /* Hot-loop batch_h: repeat N times to measure steady-state */
    printf("\n--- batch_h x 100 iterations (steady-state hot loop) ---\n");
    RdMsr(MSR_SMI_COUNT, &smi_a);
    QueryPerformanceCounter(&t0);
    int hot_ok = 0;
    for (int it = 0; it < 100; it++) {
        hot_ok += ReadVirtBatchH(items_h, buf_ptrs, N);
    }
    QueryPerformanceCounter(&t1);
    RdMsr(MSR_SMI_COUNT, &smi_b);
    double hot_total = ms(t0, t1, freq);
    uint64_t hot_smis = (smi_b - smi_a) - 1;
    printf("  total_ok=%d/%u  total=%.2f ms  per_batch=%.3f ms  SMIs=%llu (expected 100)\n",
           hot_ok, N * 100, hot_total, hot_total / 100, (unsigned long long)hot_smis);

    SmmCloseProcess(handle);

    /* === Summary === */
    printf("\n--- summary ---\n");
    printf("  serial:  %.2f ms, %llu SMIs (1 read per SMI)\n",
           s_total, (unsigned long long)s_smis);
    printf("  batch:   %.2f ms, %llu SMIs (group-by-pid cache)\n",
           b_total, (unsigned long long)b_smis);
    printf("  batch_h: %.2f ms, %llu SMIs (process handle, no lookup)\n",
           h_total, (unsigned long long)h_smis);
    if (h_total > 0) {
        printf("\n  batch_h vs serial: %.1fx faster, %lldx fewer SMIs\n",
               s_total / h_total, (long long)s_smis / (long long)(h_smis ? h_smis : 1));
        printf("  batch_h vs batch:  %.2fx faster (handle skips %u EPROCESS walks)\n",
               b_total / h_total, N);
    }
    printf("\n  hot-loop @60Hz with batch_h(32): ~%.0f SMIs/sec (60 SMIs/sec)\n",
           60.0);
    printf("  hot-loop @60Hz with serial:       %u SMIs/sec\n", N * 60);

    /* Data integrity: handle path should match pid path */
    unsigned char verify[64];
    if (ReadVirt(exp.Pid, items_pid[0].Va, verify, 64)) {
        int match = memcmp(verify, bufs[0], 64) == 0;
        printf("\n  data integrity (batch_h[0] vs ReadVirt): %s\n",
               match ? "MATCH" : "MISMATCH");
    }

    Close();
    printf("\n=== done ===\n");
    return 0;
}
