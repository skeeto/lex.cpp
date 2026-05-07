%option noyywrap
%{
#include <stdio.h>
%}
%%
^foo        printf("[BOL:%s]", yytext);
foo         printf("(%s)", yytext);
"reset"     { yy_set_bol(1); printf("[reset]"); }
"check"     { printf("[atbol=%d]", YY_AT_BOL() ? 1 : 0); }
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
