%option noyywrap
DIGIT [0-9]
%%
{DIGIT}+    printf("N(%s) ", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
