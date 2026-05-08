%option noyywrap
%%
[[:blank:]]+    printf("[B:%d]", yyleng);
[a-z]+          printf("[W:%s]", yytext);
.|\n            /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
