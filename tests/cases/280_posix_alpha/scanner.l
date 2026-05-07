%option noyywrap
%%
[[:alpha:]]+    printf("A(%s) ", yytext);
.|\n            /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
