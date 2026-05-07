%option noyywrap
%x COMMENT
%%
"/*"            { BEGIN(COMMENT); }
<COMMENT>"*/"   { BEGIN(INITIAL); printf("[CMT]"); }
<COMMENT>.|\n   /* skip */
[a-z]+          printf("(%s)", yytext);
.|\n            ECHO;
%%
int main(void){ yylex(); return 0; }
