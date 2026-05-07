%option noyywrap
%%
\r          printf("[CR]");
\f          printf("[FF]");
\v          printf("[VT]");
\a          printf("[BEL]");
\'          printf("[SQ]");
\/          printf("[SL]");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
