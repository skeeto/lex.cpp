%option noyywrap
%{
static int included = 0;
%}
%%
"INCLUDE"   {
    if (!included) { included = 1; yy_scan_string(" alpha beta "); }
}
[a-z]+      printf("(%s)", yytext);
[ \n]+      /* skip */
<<EOF>>     {
    yypop_buffer_state();
    if (!YY_CURRENT_BUFFER) yyterminate();
}
%%
int main(void){ yylex(); printf("\n"); return 0; }
