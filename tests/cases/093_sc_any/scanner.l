%option noyywrap
%x X
%%
"X"             BEGIN(X);
"Y"             BEGIN(INITIAL);
<*>[0-9]+       printf("N(%s)", yytext);
<X>[a-z]+       printf("[X:%s]", yytext);
[a-z]+          printf("(%s)", yytext);
.|\n            ECHO;
%%
int main(void){ yylex(); return 0; }
