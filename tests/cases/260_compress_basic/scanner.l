%option noyywrap
%%
[a-zA-Z]+   printf("W:%s ", yytext);
[0-9]+      printf("N:%s ", yytext);
[ \t\n]+    /* skip */
.           printf("?");
%%
int main(void){ yylex(); return 0; }
