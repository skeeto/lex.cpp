%option noyywrap yylineno
%%
[a-z]+      printf("L%d:%s ", yylineno, yytext);
.|\n        /* eat */
%%
int main(void){ yylex(); return 0; }
