%option noyywrap
%%


[a-z]+      printf("[%s]", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
