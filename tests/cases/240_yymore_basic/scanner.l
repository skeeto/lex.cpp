%option noyywrap
%%
foo  { yymore(); }
bar  { printf("[%s]", yytext); }
.|\n /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
