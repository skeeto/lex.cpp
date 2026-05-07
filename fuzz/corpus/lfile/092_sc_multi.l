%option noyywrap
%x A B
%%
"<A>"           BEGIN(A);
"<B>"           BEGIN(B);
"</>"           BEGIN(INITIAL);
<A,B>[a-z]+     printf("[%s]", yytext);
[a-z]+          printf("(%s)", yytext);
.|\n            ECHO;
%%
int main(void){ yylex(); return 0; }
