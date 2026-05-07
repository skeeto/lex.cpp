%option noyywrap
%%
[^a-z\n]+   printf("[NOT:%s]", yytext);
[a-z]+      printf("(%s)", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); return 0; }
