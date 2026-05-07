%option reentrant noyywrap
%%
[a-z]+      { printf("(%s)", yyget_text_r(yyscanner)); }
[0-9]+      { printf("[%d]", yyget_leng_r(yyscanner)); }
[ \n]+      /* skip */
%%
int main(void){
    yyscan_t s;
    yylex_init(&s);
    yyset_lineno_r(42, s);
    yy_scan_string("alpha 12345 beta", s);
    yylex(s);
    printf("\nlineno=%d\n", yyget_lineno_r(s));
    yylex_destroy(s);
    return 0;
}
