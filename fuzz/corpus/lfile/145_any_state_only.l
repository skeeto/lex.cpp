%option noyywrap
%x A
%%
"A"             BEGIN(A);
"E"             BEGIN(INITIAL);
<*>[0-9]+       printf("N(%s) ", yytext);
[a-z]+          printf("(i:%s) ", yytext);
<A>[a-z]+       printf("(a:%s) ", yytext);
.|\n            /* drop */
%%
int main(void){ yylex(); return 0; }
