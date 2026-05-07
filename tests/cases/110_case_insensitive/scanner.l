%option noyywrap case-insensitive
%%
if          printf("[IF]");
[a-z]+      printf("(%s)", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
