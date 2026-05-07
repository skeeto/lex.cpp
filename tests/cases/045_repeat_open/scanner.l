%option noyywrap
%%
a{2,}       printf("[%d]", (int)yyleng);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
