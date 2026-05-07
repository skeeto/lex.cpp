%option noyywrap
%%
[a-z]+$     printf("[E:%s]", yytext);
[a-z]+      printf("(%s)", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
