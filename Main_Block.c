#include <stdio.h>
#include "FD.c"
#include "Insurance.c"
#include "Loan.c"
#include "Registration.c"
int main()
{
    int user_input, user;
    registration_driver();
    if (user == 1)
    {
        goto the_end;
    }
start:
    printf("\n\n\x1b[1;36mWhat would you like to do?\n");

    printf("\x1b[1;36m1. Create an FD\n2. Apply for Loan\n3. Apply for Insurance\n4. Exit\n\x1b[1;34mYour Choice: \x1b[0m");
enter_2:
    scanf("%d", &user_input);

    switch (user_input)
    {
    case 1:
        fd_driver();
        goto start;
        break;

    case 2:
        loan_driver();
        goto start;
        break;

    case 3:
        insurance_driver();
        goto start;
        break;

    case 4:
        goto the_end;
        break;

    default:
        printf("\x1b[1;31mInvalid input!\n\x1b[1;36mEnter a valid choice:\x1b[0m ");
        goto enter_2;
    }

the_end:
    printf("\n\x1b[1;33mDo visit us again..\x1b[0m");
    return 0;
}
