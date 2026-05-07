%option noyywrap
%{
#include <stdio.h>
static int n = 0;
%}
%%
[a-z]+      { n++; printf("[%d:%s]", n, yytext); }
.|\n        ECHO;
%%
int main(void){ yylex(); printf("\ntotal=%d\n", n); return 0; }
