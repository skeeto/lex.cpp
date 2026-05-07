/*
 * Default yywrap for non-reentrant flex/lex scanners.
 *
 * Returns 1 (= "no further input"). Pulled in only if the user's
 * .l doesn't already define yywrap or use %option noyywrap.
 */
int yywrap(void) { return 1; }
