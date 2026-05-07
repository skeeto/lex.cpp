/*
 * Driver for the yytables_fload roundtrip test. argv[1] is the .tbl
 * path; everything else flows through stdin like a normal scanner.
 */
#include <stdio.h>
extern int yytables_fload(const char *path);
extern int yylex(void);
int yywrap(void) { return 1; }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <tables.tbl>\n", argv[0]);
        return 1;
    }
    if (yytables_fload(argv[1]) != 0) {
        fprintf(stderr, "yytables_fload failed for %s\n", argv[1]);
        return 1;
    }
    yylex();
    printf("\n");
    return 0;
}
