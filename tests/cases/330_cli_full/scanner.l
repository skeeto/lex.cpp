%option noyywrap
%%
[a-z]+      printf("(W:%s)", yytext);
[0-9]+      printf("(N:%s)", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
