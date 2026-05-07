%option noyywrap array
%%
[a-z]+      printf("(%s)", yytext);
[ \t\n]+    /* skip */
.           printf("?");
%%
int main(void){ yylex(); printf("\n"); return 0; }
