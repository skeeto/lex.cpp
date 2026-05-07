%option reentrant bison-bridge bison-locations noyywrap yylineno
%{
typedef union { int n; int len; } YYSTYPE;
typedef struct { int first_line; int last_line; } YYLTYPE;
%}
%%
[a-z]+      {
    yylval->len = (int)yyleng;
    yylloc->first_line = yylineno;
    yylloc->last_line  = yylineno;
    return 1;
}
\n          /* skip */
.           /* skip */
%%
int main(void){
    yyscan_t s;
    YYSTYPE val;
    YYLTYPE loc;
    int t;
    yylex_init(&s);
    yy_scan_string("foo\nbar\nbaz", s);
    while ((t = yylex(&val, &loc, s)) != 0)
        printf("[T%d:len=%d,L%d]", t, val.len, loc.first_line);
    yylex_destroy(s);
    printf("\n");
    return 0;
}
