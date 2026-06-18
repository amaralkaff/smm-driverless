#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Api.h"

int main(int argc, char **argv) {
    PROCESS_INFO proc;
    MODULE_INFO mod;
    uint64_t pa = 0, addr = 0;
    DWORD self_pid = GetCurrentProcessId();
    const char *target = (argc > 1) ? argv[1] : "explorer.exe";

    printf("=== SmmMem test (LATEST) ===\n");
    if (!Init()) { printf("Init failed\n"); return 1; }
    if (!Ping()) { printf("Ping failed\n"); Close(); return 1; }
    printf("ping  : pong\n");

    printf("self pid=%u\n", self_pid);

    printf("\n--- FindProcessByPid(4 = System) ---\n");
    if (FindProcessByPid(4, &proc)) {
        printf("OK pid=%u eprocess=0x%016llX cr3=0x%016llX base=0x%016llX name=%s\n",
               proc.Pid, (unsigned long long)proc.Eprocess, (unsigned long long)proc.Cr3,
               (unsigned long long)proc.ImageBase, proc.Name);
    } else {
        printf("FAILED\n");
    }

    printf("\n--- FindProcessByPid(self=%u) ---\n", self_pid);
    if (FindProcessByPid(self_pid, &proc)) {
        printf("OK pid=%u eprocess=0x%016llX cr3=0x%016llX base=0x%016llX name=%s\n",
               proc.Pid, (unsigned long long)proc.Eprocess, (unsigned long long)proc.Cr3,
               (unsigned long long)proc.ImageBase, proc.Name);
    } else {
        printf("FAILED\n");
    }

    printf("\n--- FindProcessByName(\"%s\") ---\n", target);
    if (FindProcessByName(target, &proc)) {
        printf("OK pid=%u eprocess=0x%016llX cr3=0x%016llX base=0x%016llX name=%s\n",
               proc.Pid, (unsigned long long)proc.Eprocess, (unsigned long long)proc.Cr3,
               (unsigned long long)proc.ImageBase, proc.Name);

        printf("\n--- TranslateVirt(pid, ImageBase) ---\n");
        if (TranslateVirt(proc.Pid, proc.ImageBase, &pa)) {
            printf("OK va=0x%016llX -> pa=0x%016llX\n",
                   (unsigned long long)proc.ImageBase, (unsigned long long)pa);
        } else { printf("FAILED\n"); }

        printf("\n--- ReadVirt(pid=%u, base, 64 bytes) ---\n", proc.Pid);
        unsigned char buf[64] = {0};
        if (ReadVirt(proc.Pid, proc.ImageBase, buf, sizeof(buf))) {
            printf("OK MZ=%02X%02X bytes[0..15]= ", buf[0], buf[1]);
            for (int i = 0; i < 16; i++) printf("%02X ", buf[i]);
            printf("\n");
        } else { printf("FAILED\n"); }
    } else {
        printf("FAILED\n");
    }

    printf("\n--- FindKernelModule(\"ntoskrnl.exe\") ---\n");
    if (FindKernelModule("ntoskrnl.exe", &mod)) {
        printf("OK base=0x%016llX size=0x%X cr3=0x%016llX name=%s\n",
               (unsigned long long)mod.Base, mod.Size,
               (unsigned long long)mod.Cr3, mod.Name);

        printf("\n--- FindExport(ntoskrnl, \"PsInitialSystemProcess\") ---\n");
        if (FindExport(&mod, "PsInitialSystemProcess", &addr)) {
            printf("OK export addr=0x%016llX\n", (unsigned long long)addr);
        } else { printf("FAILED\n"); }
    } else {
        printf("FAILED\n");
    }

    Close();
    printf("\n=== done ===\n");
    return 0;
}
