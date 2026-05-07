%option reentrant noyywrap
%%
[a-z]+      printf("(%s)", yytext);
[ \n]+      /* skip */
%%
int main(void){
    yyscan_t a, b;
    yylex_init(&a);
    yylex_init(&b);
    yy_scan_string("aaa bbb",  a);
    yy_scan_string("xx yy zz", b);
    printf("A:"); yylex(a); printf("\n");
    printf("B:"); yylex(b); printf("\n");
    yylex_destroy(a);
    yylex_destroy(b);
    return 0;
}
