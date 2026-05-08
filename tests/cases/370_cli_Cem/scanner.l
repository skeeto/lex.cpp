%option noyywrap
%%
[a-zA-Z][a-zA-Z0-9_]*  printf("[I:%s]", yytext);
[0-9]+                 printf("[N:%s]", yytext);
.|\n                   /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
