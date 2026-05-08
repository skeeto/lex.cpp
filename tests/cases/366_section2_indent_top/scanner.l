%option noyywrap
%%
    /* indented prologue: declared inside yylex */
    static int local_count = 0;
[a-z]+      { ++local_count; printf("[%d:%s]", local_count, yytext); }
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
