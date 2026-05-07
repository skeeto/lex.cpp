%option noyywrap
%%
[\xc0-\xff][\x80-\xbf]+   printf("[U:%d]", (int)yyleng);
[a-z]+                    printf("(%s)", yytext);
.|\n                      /* drop */
%%
int main(void){ yylex(); printf("\n"); return 0; }
