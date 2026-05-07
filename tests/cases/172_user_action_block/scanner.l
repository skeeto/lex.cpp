%option noyywrap
%{
static int total = 0;
static int matches = 0;
#define YY_USER_ACTION do { total += yyleng; matches += 1; } while (0);
%}
%%
[a-z]+      printf("(%s)", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\ntotal=%d matches=%d\n", total, matches); return 0; }
