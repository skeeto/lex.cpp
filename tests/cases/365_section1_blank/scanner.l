%option noyywrap

DIGIT   [0-9]

%{
#include <stdio.h>
%}

LETTER  [a-zA-Z]
   /* indented C comment in section 1 */
    static int seen = 0;

%%
{LETTER}+   { ++seen; printf("[L:%s]", yytext); }
{DIGIT}+    printf("[D:%s]", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\nseen=%d\n", seen); return 0; }
