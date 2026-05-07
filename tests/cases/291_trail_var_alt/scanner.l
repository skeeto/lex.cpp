%option noyywrap
%%
"if"/[ \t]+      printf("[KW:%s]", yytext);
"if"             printf("(plain-if:%s)", yytext);
.|\n             ECHO;
%%
int main(void){ yylex(); return 0; }
