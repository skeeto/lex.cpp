%option noyywrap
%%
[a-z]+      printf("(%d:%s)", (int)yyleng, yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
