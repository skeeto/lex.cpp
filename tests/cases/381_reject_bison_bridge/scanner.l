%option reentrant bison-bridge noyywrap
%{
typedef union { int n; const char *s; } YYSTYPE;
#define YYSTYPE_IS_DECLARED 1
%}
%%
[a-z]+      { yylval->s = yyget_text(yyscanner); printf("[%s]", yylval->s); REJECT; }
[a-z]+      { yylval->s = yyget_text(yyscanner); printf("(%s)", yylval->s); return 1; }
.|\n        /* skip */
%%
int main(void){
    yyscan_t s;
    YYSTYPE val;
    int t;
    yylex_init(&s);
    while ((t = yylex(&val, s)) != 0) {}
    yylex_destroy(s);
    printf("\n");
    return 0;
}
