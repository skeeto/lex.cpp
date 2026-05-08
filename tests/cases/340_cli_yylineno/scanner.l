%option noyywrap
%%
[a-z]+      printf("[%s@%d]", yytext, yylineno);
\n          /* skip */
.           /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
