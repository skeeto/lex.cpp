%option noyywrap
%x X
%%
"["         BEGIN(X);
<X>"]"      BEGIN(INITIAL);
<X>.        printf("X:%c ", yytext[0]);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
