%option noyywrap yylineno
%%
[a-z]+      { printf("[%s@%d]", yytext, yylineno); REJECT; }
[a-z]+      printf("(%s)", yytext);
\n          /* skip */
.           /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
