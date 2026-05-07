%option noyywrap
%%
[a-z]+      printf("(%s)", yytext);
[0-9]+      printf("[%s]", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
