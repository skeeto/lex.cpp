%option noyywrap
%%
[[:alpha:]_][[:alnum:]_]*    printf("ID(%s) ", yytext);
.|\n                          /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
