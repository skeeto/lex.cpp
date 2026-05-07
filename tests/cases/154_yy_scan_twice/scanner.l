%option noyywrap
%%
[a-z]+      printf("(%s)", yytext);
[ \n]+      /* skip */
%%
int main(void){
    yy_scan_string("alpha beta");
    yylex();
    yy_scan_string("gamma delta");
    yylex();
    printf("\n");
    return 0;
}
