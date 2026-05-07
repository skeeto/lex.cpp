%option noyywrap
%%
[A-Za-z_][A-Za-z_0-9]*    printf("(%s)", yytext);
.|\n                       /* skip */
%%
int main(void){ yylex(); return 0; }
