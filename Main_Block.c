#include<stdio.h>
#include "FD.c"
#include "Insurance.c"
#include "Loan.c"
#include "Money_Transfer.c"
#include "Registration.c"
int main()
{
   registration_driver();
   printf("What would you like to do?\n");
   printf("1. Check Balance\n2. Deposit money\n3. Withdraw money\n");
    return 0;
}