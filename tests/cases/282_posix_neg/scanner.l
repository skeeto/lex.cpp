%option noyywrap
%%
[^[:space:]]+    printf("[%s]", yytext);
[[:space:]]+     /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
