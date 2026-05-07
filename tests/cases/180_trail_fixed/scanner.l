%option noyywrap
%%
abc/xyz     printf("[%s]", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
