%option noyywrap
%%
abc/[xy]+        printf("[E:%s]", yytext);
[a-z]+           printf("[W:%s]", yytext);
.|\n             /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
