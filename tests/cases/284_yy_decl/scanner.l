%option reentrant noyywrap
%top {
#define YY_DECL int yylex(int extra, yyscan_t yyscanner)
}
%%
[a-z]+      printf("(%s,e=%d)", yytext, extra);
[ \n]+      /* skip */
%%
int main(void){
    yyscan_t s;
    yylex_init(&s);
    yy_scan_string("alpha beta", s);
    yylex(7, s);
    yylex_destroy(s);
    printf("\n");
    return 0;
}
