/* Regression: `^pat/trailing_context` must keep its BOL anchor.
   Pre-fix the lifted-out trailing-context tree dropped the AnchorBOL
   into an inner Concat where unwrap_anchors couldn't see it, and the
   scanner cheerfully matched the rule mid-line. Caught while building
   GCC's gengtype. */
%option noyywrap
%%
^"class"/[^a-zA-Z]    { printf("[BC]"); }
[a-zA-Z_]+            /* eat identifiers */
.|\n                  /* eat everything else */
%%
int main(void){ yylex(); printf("\n"); return 0; }
