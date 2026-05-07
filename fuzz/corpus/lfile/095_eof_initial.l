%option noyywrap
%x X
%%
"X"             BEGIN(X);
"Y"             BEGIN(INITIAL);
<X>.            ECHO;
<X><<EOF>>      { printf("<X-EOF>"); yyterminate(); }
.|\n            ECHO;
<<EOF>>         { printf("<I-EOF>"); yyterminate(); }
%%
int main(void){ yylex(); return 0; }
