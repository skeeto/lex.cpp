%option noyywrap
%%
[[:graph:]]+    printf("[G:%s]", yytext);
[[:space:]]+    /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
