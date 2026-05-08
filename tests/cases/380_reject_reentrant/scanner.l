%option reentrant noyywrap
%%
[a-z]+      { printf("[%s]", yyget_text(yyscanner)); REJECT; }
[a-z]+      printf("(%s)", yyget_text(yyscanner));
.|\n        /* skip */
%%
int main(void){
    yyscan_t s;
    yylex_init(&s);
    yylex(s);
    yylex_destroy(s);
    printf("\n");
    return 0;
}
