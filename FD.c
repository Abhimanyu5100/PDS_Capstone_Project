#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
// Function for Calculating Interest, which is getting after maturity, in fixed interval of time
float CalculateInterestRate(int duration, int isSeniorCitizen)
{
    if (isSeniorCitizen)
    {
        if (duration > 0 && duration < 6)
            return 6.00;
        else if (duration >= 6 && duration < 12)
            return 6.50;
        else if (duration >= 12 && duration < 18)
            return 7.00;
        else if (duration >= 18 && duration < 24)
            return 7.50;
        else if (duration >= 24 && duration < 30)
            return 7.75;
        else if (duration >= 30 && duration < 36)
            return 8.00;
        else if (duration >= 36 && duration < 42)
            return 8.25;
        else if (duration >= 42 && duration < 48)
            return 8.50;
        else if (duration >= 48 && duration < 54)
            return 8.75;
        else
            return 9.00;
    }
    else
    {
        if (duration > 0 && duration < 6)
            return 5.50;
        else if (duration >= 6 && duration < 12)
            return 6.00;
        else if (duration >= 12 && duration < 18)
            return 6.50;
        else if (duration >= 18 && duration < 24)
            return 7.00;
        else if (duration >= 24 && duration < 30)
            return 7.25;
        else if (duration >= 30 && duration < 36)
            return 7.50;
        else if (duration >= 36 && duration < 42)
            return 7.75;
        else if (duration >= 42 && duration < 48)
            return 8.00;
        else if (duration >= 48 && duration < 54)
            return 8.25;
        else
            return 8.50;
    }
}

// Function for Calculating Interest, which is getting before maturity, in fixed interval of time

float CalculatePreMaturityAmount(int duration, int isSeniorCitizen)
{
    if (isSeniorCitizen)
    {
        if (duration > 0 && duration < 12)
            return 5.50;
        else if (duration >= 12 && duration < 24)
            return 6.00;
        else if (duration >= 24 && duration < 36)
            return 6.50;
        else if (duration >= 36 && duration < 48)
            return 7.00;
        else
            return 7.50;
    }
    else
    {
        if (duration > 0 && duration < 12)
            return 5.00;
        else if (duration >= 12 && duration < 24)
            return 5.50;
        else if (duration >= 24 && duration < 36)
            return 6.00;
        else if (duration >= 36 && duration < 48)
            return 6.50;
        else
            return 7.00;
    }
}
void loading_screen()
{
    int i;
    int n = 3; // Number of iterations or steps in your task

    printf("\x1b[1mloading ");

    for (i = 0; i < n; i++)
    {
        printf(".");
        fflush(stdout); // Flush the output to the console
        Sleep(500);     // Sleep for 1 second
    }
    printf("\r");
    for (int j = 0; j < 14; j++)
    { // Adjust the number of spaces as needed
        printf(" ");
    }
    printf("\n");

    fflush(stdout);
}

void FD()
{
    srand(time(NULL)); // seed random number
    int AccountNo, duration, isSeniorCitizen, withdrawalOption, accountNumber = 0;
    float amount, maturityAmount, interestRate, interest, maturityAmount1;
    char name[100];
    int withdrawalDuration;

     printf("\n\n\x1b[1;33m                                                             WELCOME TO THE FD SECTION\x1b[0m\n");


name:
    
    printf("\nEnter your Account number from which you want to deposit amount for FD : ");
    scanf("%0200d", &AccountNo); // Input of Account Number

// Using of goto function to recall in scenerio of invalid duration
retryAmount:
{
    printf("\nEnter the initial deposit amount(not more than 2 crore): Rs ");
    scanf("%f", &amount); // Input of Initial Amount i.e deposit
}

    if (amount < 0 || amount > 20000000)
    {
        printf("Invalid deposit amount. Please enter a valid amount between 0 and 2 crore.\n");
        goto retryAmount;
    }
    float amount1 = amount;
// Using of goto function to recall in scenerio of invalid duration
retryDuration:
{
    printf("\nEnter the duration in months(not more than 60 months): ");
    scanf("%d", &duration); //  Input of Duration
}

    if (duration < 0 || duration > 60)
    {
        printf("Invalid duration. Please enter a valid duration between 1 and 60 months.\n");
        goto retryDuration;
    }
SeniorCitizen:
{
    printf("\nEnter 1 if you are a senior citizen, otherwise enter 0: ");
    scanf("%d", &isSeniorCitizen); // Checking That account holder is senior citizen or not
}
    if (isSeniorCitizen != 0 && isSeniorCitizen != 1)
    {
        printf("Invalid choice for senior citizen status. Please enter 0 or 1.\n");
        goto SeniorCitizen;
    }
    printf("-----------------------------------------------------------------------------------------------------------\n");
    loading_screen();
    // Showing percentage return (after maturity) to Account holder
    printf("\nExpected percentage return are as follows : ");
    if (isSeniorCitizen)
    {
        printf("\n0 - 6 months = 6.00 p.a");
        printf("\n6 -12 months = 6.50 p.a");
        printf("\n12 - 18 months = 7.0 p.a");
        printf("\n18 - 24 months = 7.5 p.a");
        printf("\n24 - 30 months = 7.75 p.a ");
        printf("\n30 - 36 months = 8.0 p.a");
        printf("\n36 - 42 months = 8.25 p.a ");
        printf("\n42 - 48 months = 8.50 p.a");
        printf("\n48 - 54 months = 8.75 p.a");
        printf("\n54 - 60 months = 9.0 p.a");
    }
    else
    {
        printf("\n0 - 6 months = 5.5 p.a");
        printf("\n6 -12 months = 6.0 p.a");
        printf("\n12 - 18 months = 6.5 p.a");
        printf("\n18 - 24 months = 7.0 p.a");
        printf("\n24 - 30 months = 7.25 p.a");
        printf("\n30 - 36 months = 7.5 p.a");
        printf("\n36 - 42 months = 8.75 p.a");
        printf("\n42 - 48 months = 8.00 p.a");
        printf("\n48 - 54 months = 8.25 p.a");
        printf("\n54 - 60 months = 8.50p.a");
    }
    printf("\n----------------------------------------------------------------------------------------------------------");
    // Showing percentage return (before maturity) to Account holder

    while (1)
    {
        printf("\nEnter 1 if you want to know your return after maturity");
        printf("\nEnter 2 if you want to know your return before maturity");
        printf("\nEnter 3 for proceed ");
        printf("\nEnter 0 for exit\nYour Choice: ");
        scanf("%d", &withdrawalOption);

        if (withdrawalOption == 0)
        {
            printf("\nYour FD application is cancelled");
            break;
        }
        else if (withdrawalOption == 3)
        {
            printf("-------------------------------------------------------------------------\n");
            loading_screen();
            printf("\nYour FD account has been created and your details are follows : ");

            accountNumber = rand() % 9000000000 + 1000000000;

            printf("\nNew FD account - %010d", accountNumber);
            printf("\n\nTotal Principle Ammount - Rs %.2f", amount1);
            printf("\n\nDuration - %d months", duration);
            interestRate = CalculateInterestRate(duration, isSeniorCitizen);
            printf("\n\nInterest Rate - %.2f ", interestRate);
            maturityAmount1 = amount1 + amount1 * duration * interestRate / 1200.0;
            printf("\n\nExpectaion return after maturity period - Rs %.2f\n\n", maturityAmount1);
            break;
        }
        switch (withdrawalOption)
        {
        case 1:
            printf("-------------------------------------------------------------------------\n");

            loading_screen();
            interestRate = CalculateInterestRate(duration, isSeniorCitizen);
            interest = amount * duration * interestRate / 1200.0;
            maturityAmount = amount + interest;
            printf("\nExpected Maturity Amount after %d months: %.2f\n", duration, maturityAmount);
            break;

        case 2:
            printf("-------------------------------------------------------------------------\n");
            loading_screen();
            printf("\n\nBefore maturity period the expected percentage return are as follows : ");
            if (isSeniorCitizen)
            {
                printf("\n0-1 year = 5.5 p.a ");
                printf("\n1-2 year = 6.0 p.a ");
                printf("\n2-3 year = 6.5 p.a ");
                printf("\n3-4 year = 7.0 p.a ");
                printf("\n4-5 year = 7.5 p.a\n");
            }
            else
            {
                printf("\n0-1 year = 5.0 p.a ");
                printf("\n1-2 year = 5.5 p.a ");
                printf("\n2-3 year = 6.0 p.a ");
                printf("\n3-4 year = 6.5 p.a ");
                printf("\n4-5 year = 7.0 p.a\n");
            }

            printf("\nEnter the duration in month for which you want to know the expected return: ");
            scanf("%d", &withdrawalDuration);
            if (withdrawalDuration >= duration)
            {
                printf("Invalid withdrawal duration. Please enter a duration lesser than the original deposit duration.\n");
                break;
            }
            interestRate = CalculatePreMaturityAmount(withdrawalDuration, isSeniorCitizen);
            interest = amount * withdrawalDuration * interestRate / 1200.0;
            maturityAmount = amount + interest;
            printf("\nMaturity Amount after withdrawal at %d months: %.2f\n", withdrawalDuration, maturityAmount);
            break;

        default:
            printf("\nInvalid choice for withdrawal option. Please enter 0 to 3.\x1b[0m\n");
            break;
        }
    }
}
int fd_driver();
int fd_driver()
{
    FD();
    return 0;
}