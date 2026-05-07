%option noyywrap
%%
(red|green|blue|yellow|orange|purple|pink|black|white|gray|brown)   printf("[CLR:%s]", yytext);
[a-z]+      printf("(id:%s)", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
