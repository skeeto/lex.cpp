%option noyywrap
%%
"if"        { printf("[KW]"); REJECT; }
[a-z]+      printf("(%s)", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
