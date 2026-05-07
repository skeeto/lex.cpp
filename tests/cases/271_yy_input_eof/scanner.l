%option noyywrap
%{
#define YY_INPUT(buf, result, max_size) do {                        \
    (void)(buf); (void)(max_size);                                   \
    (result) = 0;                                                    \
} while (0)
%}
%%
[a-z]+      printf("(%s)", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("[done]\n"); return 0; }
