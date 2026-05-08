%option reentrant noyywrap prefix="abc"
%%
[a-z]+      printf("(%s)", abcget_text(yyscanner));
.|\n        /* skip */
%%
int main(void){
    yyscan_t s;
    abclex_init(&s);
    abclex(s);
    abclex_destroy(s);
    printf("\n");
    return 0;
}
