%option noyywrap
%%
[[:lower:]]+/[[:space:]]+   printf("[KW:%s]", yytext);
[[:lower:]]+                printf("(%s)", yytext);
.|\n                        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
