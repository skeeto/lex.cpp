%option noyywrap
%{
/* lex.cpp's runtime ships a variadic yyterminate, but flex's default
   is 0-arg. Redefine here so the differential test compiles on both. */
#undef yyterminate
#define yyterminate(...) return (0 __VA_OPT__(,) __VA_ARGS__)
%}
%%
"halt"      { yyterminate(42); }
[a-z]+      { printf("(%s)", yytext); }
.|\n        /* skip */
%%
int main(void){
    int rc = yylex();
    printf("\nrc=%d\n", rc);
    return 0;
}
