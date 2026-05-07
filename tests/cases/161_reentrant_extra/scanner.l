%option reentrant noyywrap
%option extra-type="int *"
%{
#include <stdlib.h>
%}
%%
[a-z]+      { int *cnt = yyget_extra(yyscanner); (*cnt)++; }
.|\n        /* skip */
%%
int main(void){
    yyscan_t s;
    int counter = 0;
    yylex_init_extra(&counter, &s);
    yy_scan_string("foo bar baz qux", s);
    yylex(s);
    yylex_destroy(s);
    printf("count=%d\n", counter);
    return 0;
}
