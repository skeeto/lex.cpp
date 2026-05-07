%option noyywrap
%%
^(foo|bar)  printf("[H:%s]", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
