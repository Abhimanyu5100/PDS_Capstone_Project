#include <stdio.h>
#include <windows.h> // Include the Windows API header for Sleep function

int moneytransfer_driver();
void processing_screen()
{
    int i;
    int n = 3; // Number of iterations or steps in your task

    printf("Processing ");

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

int moneytransfer_driver()
{
    int bank_account_Sender, Bank_Account_Receiver, Bank_Balance, Amount, New_Bank_balance = Bank_Balance - Amount;

    FILE *file = fopen("money_tranfer.txt", "a"); // "a" opens the file in append mode
    if (file == NULL)
    {
        printf("Error opening file for writing.\n");
    }

    printf("\nEnter Your Bank Account Number: ");
    scanf("%d", &bank_account_Sender);

    printf("\nYour Bank Details are as follows: ");
    // Function call of bank detail

    printf("\nEnter the receiver Bank Account Number: ");
    scanf("%d", &Bank_Account_Receiver);

    printf("\nEnter the Amount that you want to transfer: ");
    scanf("%d", &Amount);

    printf("\nEnter 1 for transferring money\nEnter 0 for exit\nYour choice: ");
    int your_choice;
    scanf("%d", &your_choice);
    switch (your_choice)
    {
    case 1:

        printf("------------------------------------------------------------------------\n");
        processing_screen();
        printf("Your money has been transferred to %d account.\n", Bank_Account_Receiver);
        printf("Your Balance balance after debit is %d ", New_Bank_balance);
        break;
    case 0:
        printf("\nYour Money Transfer has been cancelled ");
        break;
    default:
        printf("\nInvalid choice for withdrawal option. Please enter 0 to 3.\n");
        break;
    }

    fprintf(file, "Sender's Account Number: %d", bank_account_Sender);
    fprintf(file, "Receiver's Account Number: %d", Bank_Account_Receiver);
    fprintf(file, "Money debited from Sender.s Account = %d", Amount);
    fprintf(file, "Bank balance After debit = %d", New_Bank_balance);
    return 0;
}
