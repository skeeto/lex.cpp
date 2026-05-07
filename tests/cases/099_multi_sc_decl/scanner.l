%option noyywrap
%s INC1 INC2
%x EXC1
%x EXC2
%%
"i1"            BEGIN(INC1);
"i2"            BEGIN(INC2);
"e1"            BEGIN(EXC1);
"e2"            BEGIN(EXC2);
"r"             BEGIN(INITIAL);
<*>[a-z]+       printf("[%s]", yytext);
[0-9]+          printf("(N:%s)", yytext);
.|\n            /* drop */
%%
int main(void){ yylex(); return 0; }
