%option noyywrap nodefault
%%
[a-z]+\n    printf("(%s)", yytext);
%%
int main(void){ yylex(); return 0; }
