#include <stdio.h>
#include "answer.h"
int main(void) {
    int result = answer();
    printf("M54-APP answer=%d\n", result);
    return result > 0 ? 0 : 1;
}
