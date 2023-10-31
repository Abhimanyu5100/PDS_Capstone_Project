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
   printf("What would you like to do?\n");
   printf("1. Check Balance\n2. Deposit\n3. Withdraw\n4. Create an FD\n5. Apply for Loan\n6. Apply for Insurance\n7. Exit\n");
   printf("Your choice: "); re_enter:
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
    break;

    case 6:
    insurance_driver();
    break;

    case 7:
    printf("\nDo visit again..");
    goto the_end;
    break;

    default:
    printf("Invalid input!\nEnter a valid choice: ");
    goto re_enter;
   }
the_end:
    return 0;
}