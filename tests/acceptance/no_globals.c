/*
 * Zero mutable globals, enforced rather than asserted in prose.
 *
 * "No file-scope mutable globals" is this library's first design constraint,
 * and it has been broken twice: a pluggable allocator's function-pointer table,
 * and a once-guard in the SIGPIPE helper. Both were removed. This test is what
 * keeps the next one from arriving unnoticed.
 *
 * It reads the compiled objects rather than the source, so a macro, an #ifdef,
 * or a declaration that never becomes storage cannot fool it. A symbol counts
 * as mutable only if it lands in .data or .bss. It does not count in .rodata or
 * .data.rel.ro -- the latter is writable only until the loader relocates it,
 * which is how the transport vtables stay const.
 *
 * objdump does the reading. Parsing its text is boring and works everywhere a
 * C compiler does; parsing ELF byte offsets in here would not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Run objdump -t on one object and report any symbol sitting in a mutable
 * section. Returns the number of violations found. */
static int violations_in(const char* object_path, const char* label, int* saw_symbols) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "objdump -t '%s' 2>/dev/null", object_path);

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        printf("FAIL: cannot run objdump on %s\n", object_path);
        return -1;
    }

    int violations = 0;
    char line[1024];
    while (fgets(line, sizeof(line), pipe)) {
        /* Format: "<addr> <flags> <section> <size> <name>".
         * Only defined, local-or-global object symbols matter; *UND* is a
         * reference and l (local) is fine -- a static is still file-scope. */
        char section[64];
        unsigned long long size = 0;
        char name[256];
        char flags[32];
        /* "<addr> <flags> <kind> <section> <size> <name>" -- the kind column
         * is O (object), F (func) or *UND*; skipping it is what makes this
         * read the section rather than treating it as the size. */
        int fields = sscanf(line, "%*s %31s %*s %63s %llx %255s",
                            flags, section, &size, name);
        if (fields != 4) continue;
        if (strstr(flags, "*UND*")) continue;

        /* .data.rel.ro is read-only after relocation, so it is deliberately
         * absent: a const vtable belongs there. */
        (*saw_symbols)++;
        int mutable = (strcmp(section, ".data") == 0) ||
                      (strcmp(section, ".bss") == 0);
        if (mutable && size > 0) {
            printf("FAIL: %s: %s (%llu bytes) is in %s\n", label, name, size, section);
            violations++;
        }
    }
    pclose(pipe);
    return violations;
}

int main(int argc, char** argv) {
    printf("=== Zero mutable globals ===\n");

    if (argc < 2) {
        printf("FAIL: no object files given; the check proved nothing\n");
        return 1;
    }

    /* CMake hands $<TARGET_OBJECTS:uvrpc> over as one semicolon-joined
     * string, so each argument may carry several paths. */
    int violations = 0;
    int objects = 0;
    int unreadable = 0;
    for (int i = 1; i < argc; i++) {
        char* list = strdup(argv[i]);
        for (char* path = strtok(list, ";"); path; path = strtok(NULL, ";")) {
            int saw = 0;
            int v = violations_in(path, path, &saw);
            if (v < 0) return 1;
            if (saw == 0) {
                /* objdump on a missing file says nothing and exits 0, so an
                 * unreadable object would otherwise pass silently -- a check
                 * that cannot fail is worse than no check. */
                printf("FAIL: %s produced no symbol table\n", path);
                unreadable++;
            }
            violations += v;
            objects++;
        }
        free(list);
    }

    if (unreadable > 0) return 1;

    if (violations == 0) {
        printf("[PASS] %d translation units, no file-scope mutable state\n", objects);
        return 0;
    }
    printf("%d violation(s). A file-scope mutable global is the one thing this\n"
           "library does not have.\n", violations);
    return 1;
}
