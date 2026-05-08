%option noyywrap
%%
[A-Z][a-z]+ printf("[Word:%s]", yytext);
[0-9]+      printf("[Num:%s]", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
