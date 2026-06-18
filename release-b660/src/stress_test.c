#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Api.h"

static void hex16(const unsigned char *b) {
    for (int i = 0; i < 16; i++) printf("%02X ", b[i]);
    printf("\n");
}

static double time_ms(LARGE_INTEGER s, LARGE_INTEGER e, LARGE_INTEGER f) {
    return (double)(e.QuadPart - s.QuadPart) * 1000.0 / (double)f.QuadPart;
}

int main(void) {
    PROCESS_INFO self;
    LARGE_INTEGER freq, t0, t1;
    DWORD self_pid = GetCurrentProcessId();
    QueryPerformanceFrequency(&freq);

    printf("=== SmmMem stress test ===\n");
    if (!Init() || !Ping()) { printf("init failed\n"); return 1; }
    printf("ping ok\n");

    /* === Test 1: long process names (>15 chars) === */
    printf("\n--- Test 1: long process name resolution (>15 chars) ---\n");
    const char *long_names[] = {
        "ApplicationFrameHost.exe",  /* 24 chars */
        "RuntimeBroker.exe",         /* 17 chars */
        "SecurityHealthService.exe", /* 25 chars */
        "WindowsTerminal.exe",       /* 19 chars */
        "SearchIndexer.exe",         /* 17 chars */
        NULL
    };
    for (int i = 0; long_names[i]; i++) {
        PROCESS_INFO p;
        printf("  %-30s ", long_names[i]);
        if (FindProcessByName(long_names[i], &p)) {
            printf("OK pid=%-6u name=%s\n", p.Pid, p.Name);
        } else {
            printf("not found (process may not be running)\n");
        }
    }

    /* === Test 2: WriteVirt round-trip into our own memory === */
    printf("\n--- Test 2: WriteVirt round-trip (own process buffer) ---\n");
    if (!FindProcessByPid(self_pid, &self)) {
        printf("FAILED to find self\n"); Close(); return 1;
    }
    unsigned char canary[64];
    for (int i = 0; i < 64; i++) canary[i] = (unsigned char)(0xA0 + (i & 0xF));
    uint64_t buf_va = (uint64_t)(uintptr_t)&canary[0];
    printf("  buffer va=0x%016llX cr3=0x%016llX\n",
           (unsigned long long)buf_va, (unsigned long long)self.Cr3);
    printf("  initial: "); hex16(canary);

    unsigned char readback[64] = {0};
    if (!ReadVirt(self_pid, buf_va, readback, 64)) {
        printf("  ReadVirt FAILED\n");
    } else {
        printf("  via SMM: "); hex16(readback);
        if (memcmp(readback, canary, 64) == 0) printf("  read matches\n");
        else printf("  READ MISMATCH\n");
    }

    unsigned char newval[64];
    for (int i = 0; i < 64; i++) newval[i] = (unsigned char)(0xDE ^ i);
    if (!WriteVirt(self_pid, buf_va, newval, 64)) {
        printf("  WriteVirt FAILED\n");
    } else {
        printf("  after Write (in-process view): "); hex16(canary);
        if (memcmp(canary, newval, 64) == 0) printf("  write round-trip OK\n");
        else printf("  WRITE MISMATCH\n");
    }

    /* === Test 3: read perf benchmark === */
    printf("\n--- Test 3: read performance benchmark ---\n");
    PROCESS_INFO exp;
    if (!FindProcessByName("explorer.exe", &exp)) {
        printf("  no explorer, skip\n");
    } else {
        unsigned char bigbuf[4096];
        uint64_t va = exp.ImageBase;
        struct { uint32_t size; const char *label; } cases[] = {
            {64,    "  64B "},
            {256,   "  256B"},
            {352,   "  352B (one SMI max)"},
            {1024,  "  1KB "},
            {4096,  "  4KB (12 SMIs)    "},
            {0, NULL}
        };
        for (int c = 0; cases[c].size; c++) {
            const int iters = 50;
            QueryPerformanceCounter(&t0);
            int ok = 1;
            for (int i = 0; i < iters; i++) {
                if (!ReadVirt(exp.Pid, va, bigbuf, cases[c].size)) { ok = 0; break; }
            }
            QueryPerformanceCounter(&t1);
            double total = time_ms(t0, t1, freq);
            printf("  %s  x%d iters  total=%7.2f ms  per_op=%6.3f ms\n",
                   cases[c].label, iters, total, total / iters);
            if (!ok) printf("    (some iters FAILED)\n");
        }
    }

    Close();
    printf("\n=== done ===\n");
    return 0;
}
