%option noyywrap
%%
%{
    static int call_count = 0;
    ++call_count;
%}
[a-z]+    printf("(%s,c=%d) ", yytext, call_count);
[ \n]+    /* skip */
%%
int main(void){
    yylex();
    yylex();
    printf("\n");
    return 0;
}
