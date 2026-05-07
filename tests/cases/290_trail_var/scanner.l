%option noyywrap
%%
[a-z]+/[0-9]+    printf("[%s]", yytext);
.|\n             ECHO;
%%
int main(void){ yylex(); return 0; }
