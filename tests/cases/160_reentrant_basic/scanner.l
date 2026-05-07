%option reentrant noyywrap
%%
[a-z]+      printf("(%s)", yytext);
[0-9]+      printf("[%s]", yytext);
[ \n]+      /* skip */
%%
int main(void){
    yyscan_t s;
    yylex_init(&s);
    yy_scan_string("alpha 12 beta 34", s);
    yylex(s);
    yylex_destroy(s);
    printf("\n");
    return 0;
}
