%option noyywrap
%{
static int hits = 0;
#define YY_USER_INIT { printf("[init]"); hits = 100; }
%}
%%
[a-z]+      { hits++; printf("(%s:%d)", yytext, hits); }
.|\n        /* skip */
%%
int main(void){ yylex(); return 0; }
