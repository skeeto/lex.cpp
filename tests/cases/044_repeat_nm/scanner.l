%option noyywrap
%%
a{2,4}      printf("[%d:%s]", (int)yyleng, yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
