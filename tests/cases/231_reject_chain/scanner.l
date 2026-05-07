%option noyywrap
%%
"abc"       { printf("[A]"); REJECT; }
"abc"       { printf("[B]"); REJECT; }
"abc"       { printf("[C]"); }
[a-z]+      printf("(%s)", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
