%option noyywrap
%%
a   { yymore(); }
b   { yymore(); }
c   { printf("[%s]", yytext); }
.|\n /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
