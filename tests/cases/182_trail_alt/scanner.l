%option noyywrap
%%
(if|else)/[^a-z] printf("[KW:%s]", yytext);
[a-z]+           printf("(%s)", yytext);
.|\n             ECHO;
%%
int main(void){ yylex(); return 0; }
