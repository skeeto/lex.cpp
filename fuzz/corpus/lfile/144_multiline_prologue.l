%option noyywrap
%{
#include <ctype.h>
static int counter = 0;

static int upper(char c) { return toupper((unsigned char)c); }
%}
%%
[a-z]+      { counter++; printf("[%d:%s]", counter, yytext); }
.|\n        ECHO;
%%
int main(void){ yylex(); printf("\ntotal=%d\n", counter); return 0; }
