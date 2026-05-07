/*
 * Default main() for non-reentrant flex/lex scanners.
 *
 * Drains yylex() until it returns 0 (yyterminate / EOF without action).
 * Pulled in only if the user's .l (or section 3) doesn't already
 * define main.
 */
extern int yylex(void);

int main(void) {
    while (yylex() != 0) { /* drain */ }
    return 0;
}
