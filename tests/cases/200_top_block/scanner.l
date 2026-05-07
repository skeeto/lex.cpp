%top {
#define _XOPEN_SOURCE 700
}
%option noyywrap
%{
#include <stdio.h>
%}
%%
[a-z]+      printf("(%s)", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
