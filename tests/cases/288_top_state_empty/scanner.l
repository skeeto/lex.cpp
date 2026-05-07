%option noyywrap stack
%%
.|\n  /* skip */
%%
int main(void){
    /* No prior yy_push_state -- yy_top_state on empty stack. */
    int s = yy_top_state();
    printf("unreachable: %d\n", s);
    return 0;
}
