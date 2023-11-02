#include<stdio.h>
#include "FD.c"
#include "Insurance.c"
#include "Loan.c"
#include "Money_Transfer.c"
#include "Registration.c"
int main()
{
    int user_input;
    registration_driver();
    if (registration_driver != 0)
    {goto the_end;
}
   start:
   printf("\x1b[1;36m\n\nWhat would you like to do?\n");
   printf("1. Check Balance\n2. Deposit\n3. Withdraw\n4. Create an FD\n5. Apply for Loan\n6. Apply for Insurance\n7. Exit\n");
   printf("\x1b[1;34mYour choice: \x1b[0m"); re_enter:
   scanf("%d", &user_input);

   switch(user_input)
   {
    case 1:
    //Check Balance
    break;

    case 2:
    //Deposit Money
    break;

    case 3:
    //Withdraw
    break;

    case 4:
    fd_driver();
    break;

    case 5:
    loan_driver();
    goto start;
    break;

    case 6:
    insurance_driver();
    goto start;
    break;

    case 7:
    goto the_end;
    break;

    default:
    printf("\x1b[1;31mInvalid input!\n\x1b[1;36mEnter a valid choice:\x1b[0m ");
    goto re_enter;
   }
the_end:
printf("\n\x1b[1;33mDo visit us again..\x1b[0m");
    return 0;
}


