%option noyywrap
%%
[[:punct:]]+    printf("[U:%s]", yytext);
[[:alpha:]]+    printf("[A:%s]", yytext);
.|\n            /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
