%option noyywrap
%%
^[a-z]+/[0-9]   printf("[L:%s]", yytext);
[a-z]+          printf("[W:%s]", yytext);
[0-9]+          printf("[N:%s]", yytext);
.|\n            /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
