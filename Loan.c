#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>

int loan_amount, tenure, max_loan;
int interest_rate, cibilscore, response;
float EMI, income, approved_loan;

void home_loan(int cibilscore);
void business_loan(int cibilscore);
void student_loan(int cibilscore);
void personal_loan(int cibilscore);
void loan_amount_tracker(int cibilscore);
void interest_home(int cibilscore, int loan_amount, int tenure, float income);
void interest_student(int cibilscore, int loan_amount, int tenure, float income);
void interest_personal(int cibilscore, int loan_amount, int tenure, float income);
void interest_business(int cibilscore, int loan_amount, int tenure, float income);
void final_output(int interest_rate, float approved_loan, int tenure, float EMI);
void show_loading();
void show_processing2();
void show_wait();
int loan_driver()
{
    int input;

    printf("\n\n\x1b[1;33m                                                             WELCOME TO THE LOAN SECTION\x1b[0m\n");

start:
    printf("\x1b[1;37m\nPlease enter your CIBIL score: ");
    scanf("%d", &cibilscore);
    loan_amount_tracker(cibilscore);
    show_loading();

    if (cibilscore < 550)
    {
        printf("\nWe are sorry. You are not eligible to get a loan.");
        goto end;
    }
    else if (cibilscore < 620 && cibilscore >= 550)
    {
        printf("\nYou are eligible to get a Home Loan.");
        printf("\nWhat would you like to do?");
        printf("\n1. Proceed with Home Loan\n2. Exit");
        printf("\nEnter your choice: ");
    scanA:
        scanf("%d", &input);
        printf("\n");

        if (input == 1)
            home_loan(cibilscore);
        else if (input == 2)
            goto end;
        else
        {
            printf("Please enter a valid choice: ");
            goto scanA;
        }
    }

    else if (cibilscore < 670 && cibilscore >= 620)
    {
        printf("\nYou are eligible to get these loans:");
        printf("\n1. Home Loan    2. Personal Loan\n");
        printf("\nWhat would you like to do?");
        printf("\n1. Proceed with Home loan.");
        printf("\n2. Proceed with Personal loan.");
        printf("\n3. Exit");
        printf("\nEnter your choice: ");
    scanB:
        scanf("%d", &input);
        printf("\n");

        if (input == 1)
            home_loan(cibilscore);
        else if (input == 2)
            personal_loan(cibilscore);
        else if (input == 3)
            goto end;
        else
        {
            printf("Please enter a valid choice: ");
            goto scanB;
        }
    }

    else if (cibilscore < 700 && cibilscore >= 670)
    {
        printf("\nYou are eligible to get these loans:");
        printf("\n1. Home Loan    2. Personal Loan    3. Student Loan\n");
        printf("\nWhat would you like to do?");
        printf("\n1. Proceed with Home loan.");
        printf("\n2. Proceed with Personal loan.");
        printf("\n3. Proceed with Student Loan.");
        printf("\n4. Exit");
        printf("\nEnter your choice: ");
    scanC:
        scanf("%d", &input);
        printf("\n");

        if (input == 1)
            home_loan(cibilscore);
        else if (input == 2)
            personal_loan(cibilscore);
        else if (input == 3)
            student_loan(cibilscore);
        else if (input == 4)
            goto end;
        else
        {
            printf("Please enter a valid choice: ");
            goto scanC;
        }
    }

    else if (cibilscore >= 700 && cibilscore <= 900)
    {
        printf("\nYou are eligible to get these loans:");
        printf("\n1. Home Loan    2. Personal Loan    3. Student Loan    4. Business Loan\n");
        printf("\nWhat would you like to do?");
        printf("\n1. Proceed with Home loan.");
        printf("\n2. Proceed with Personal loan.");
        printf("\n3. Proceed with Student Loan.");
        printf("\n4. Proceed with Business loan.");
        printf("\n5. Exit");
        printf("\nEnter your choice: ");
    scanD:
        scanf("%d", &input);
        printf("\n");

        if (input == 1)
            home_loan(cibilscore);
        else if (input == 2)
            personal_loan(cibilscore);
        else if (input == 3)
            student_loan(cibilscore);
        else if (input == 4)
            business_loan(cibilscore);
        else if (input == 5)
            goto end;
        else
        {
            printf("Please enter a valid choice: ");
            goto scanD;
        }
    }

    else if (cibilscore > 900)
    {
        printf("\nPlease enter a valid CIBIL score.\n");
        goto start;
    }
end:
    return 0;
}

void home_loan(int cibilscore)
{

home_loanA:
    printf("You can get a loan amounting upto %d lakhs.\n", max_loan);
    printf("Please enter loan amount in lakhs: ");
    scanf("%d", &loan_amount);
    if (loan_amount > max_loan || loan_amount < 0)
    {
        goto home_loanA;
    }
home_loanB:
    printf("Please enter the tenure for your loan(in months): ");
    scanf("%d", &tenure);
    if (tenure > 300 || tenure < 24)
    {
        printf("Maximum tenure is 300 months and minimum tenure is 24 months.\n");
        goto home_loanB;
    }
    printf("Enter your monthly income in rupees: ");
    scanf("%f", &income);

    interest_home(cibilscore, loan_amount, tenure, income);
    show_wait();
    printf("\nYou are approved a loan of Rs.%.2f for %d months at an anuual interest rate of %d%%.\nYou will have to pay an EMI of Rs.%.2f.", approved_loan, tenure, interest_rate, EMI);
redirect:
    printf("What would you like to do?\n1. Accept the loan.\n2. Exit\nYour Choice: ");
    scanf("%d", &response);

    if (response == 1)
        final_output(interest_rate, approved_loan, tenure, EMI);
    else if (response == 2)
        goto endA;
    else
    {
        printf("Please enter a valid response.\n");
        goto redirect;
    }

endA:
    printf("\n");
}

void business_loan(int cibilscore)
{
business_loanA:
    printf("You can get a loan amounting upto %d lakhs.\n", max_loan);
    printf("Please enter loan amount in lakhs: ");
    scanf("%d", &loan_amount);
    if (loan_amount > max_loan || loan_amount < 0)
    {
        goto business_loanA;
    }
business_loanB:
    printf("Please enter the tenure for your loan(in months): ");
    scanf("%d", &tenure);
    if (tenure > 300 || tenure < 24)
    {
        printf("Maximum tenure is 300 months and minimum tenure is 24 months.\n");
        goto business_loanB;
    }
    printf("Enter your monthly income in rupees: ");
    scanf("%f", &income);

    interest_business(cibilscore, loan_amount, tenure, income);
    show_wait();
    printf("\nYou are approved a loan of Rs.%.2f for %d months at an anuual interest rate of %d%%.\nYou will have to pay an EMI of Rs.%.2f.", approved_loan, tenure, interest_rate, EMI);
redirect:
    printf("What would you like to do?\n1. Accept the loan.\n2. Exit\nYour Choice: ");
    scanf("%d", &response);

    if (response == 1)
        final_output(interest_rate, approved_loan, tenure, EMI);
    else if (response == 2)
        goto endA;
    else
    {
        printf("Please enter a valid response.\n");
        goto redirect;
    }

endA:
    printf("\n");
}

void student_loan(int cibilscore)
{
student_loanA:
    printf("You can get a loan amounting upto %d lakhs.\n", max_loan);
    printf("Please enter loan amount in lakhs: ");
    scanf("%d", &loan_amount);
    if (loan_amount > max_loan || loan_amount < 0)
    {
        goto student_loanA;
    }
student_loanB:
    printf("Please enter the tenure for your loan(in months): ");
    scanf("%d", &tenure);
    if (tenure > 300 || tenure < 24)
    {
        printf("Maximum tenure is 300 months and minimum tenure is 24 months.\n");
        goto student_loanB;
    }
    printf("Enter your monthly income in rupees: ");
    scanf("%f", &income);

    interest_student(cibilscore, loan_amount, tenure, income);
    show_wait();
    printf("\nYou are approved a loan of Rs.%.2f for %d months at an anuual interest rate of %d%%.\nYou will have to pay an EMI of Rs.%.2f.", approved_loan, tenure, interest_rate, EMI);
redirect:
    printf("What would you like to do?\n1. Accept the loan.\n2. Exit\nYour Choice: ");
    scanf("%d", &response);

    if (response == 1)
        final_output(interest_rate, approved_loan, tenure, EMI);
    else if (response == 2)
        goto endA;
    else
    {
        printf("Please enter a valid response.\n");
        goto redirect;
    }

endA:
    printf("\n");
}

void personal_loan(int cibilscore)
{
personal_loanA:
    printf("You can get a loan amounting upto %d lakhs.\n", max_loan);
    printf("Please enter loan amount in lakhs: ");
    scanf("%d", &loan_amount);
    if (loan_amount > max_loan || loan_amount < 0)
    {
        goto personal_loanA;
    }
personal_loanB:
    printf("Please enter the tenure for your loan(in months): ");
    scanf("%d", &tenure);
    if (tenure > 300 || tenure < 24)
    {
        printf("Maximum tenure is 300 months and minimum tenure is 24 months.\n");
        goto personal_loanB;
    }
    printf("Enter your monthly income in rupees: ");
    scanf("%f", &income);

    interest_personal(cibilscore, loan_amount, tenure, income);
    show_wait();
    printf("\nYou are approved a loan of Rs.%.2f for %d months at an anuual interest rate of %d%%.\nYou will have to pay an EMI of Rs.%.2f.", approved_loan, tenure, interest_rate, EMI);
redirect:
    printf("What would you like to do?\n1. Accept the loan.\n2. Exit\nYour Choice: ");
    scanf("%d", &response);

    if (response == 1)
        final_output(interest_rate, approved_loan, tenure, EMI);
    else if (response == 2)
        goto endA;
    else
    {
        printf("Please enter a valid response.\n");
        goto redirect;
    }

endA:
    printf("\n");
}

void loan_amount_tracker(int cibilscore)
{
    if (cibilscore >= 550 && cibilscore < 580)
        max_loan = 20;
    else if (cibilscore >= 580 && cibilscore < 620)
        max_loan = 45;
    else if (cibilscore >= 620 && cibilscore < 650)
        max_loan = 70;
    else if (cibilscore >= 650 && cibilscore < 700)
        max_loan = 100;
    else if (cibilscore >= 700 && cibilscore <= 900)
        max_loan = 250;
    else
        max_loan = 0;
}

void interest_home(int cibilscore, int loan_amount, int tenure, float income)
{
    float a;

    if (cibilscore < 560 && cibilscore >= 550)
        interest_rate = 15;
    else if (cibilscore < 570 && cibilscore >= 560)
        interest_rate = 14;
    else if (cibilscore < 580 && cibilscore >= 570)
        interest_rate = 13;
    else if (cibilscore < 590 && cibilscore >= 580)
        interest_rate = 12;
    else if (cibilscore < 600 && cibilscore >= 590)
        interest_rate = 11;
    else if (cibilscore < 610 && cibilscore >= 600)
        interest_rate = 10;
    else if (cibilscore < 620 && cibilscore >= 610)
        interest_rate = 9;
    else
        interest_rate = 8;
    a = pow((1 + (float)interest_rate / 100), tenure / 12);

    EMI = (float)((loan_amount * 100000) * a) / tenure;

    if (income >= 100000 && EMI <= income / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income >= 100000 && EMI > income / 2)
    {
        EMI = income / 2;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 100000 && income >= 60000 && income >= EMI * 5 / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 100000 && income >= 60000 && income < EMI * 5 / 2)
    {
        EMI = income * 2 / 5;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 60000 && income >= 20000 && income >= EMI * 10 / 3)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 60000 && income >= 20000 && income < EMI * 10 / 3)
    {
        EMI = income * 3 / 10;
        approved_loan = (float)EMI * tenure / a;
    }
}
void interest_student(int cibilscore, int loan_amount, int tenure, float income)
{
    float a;

    if (cibilscore < 680 && cibilscore >= 670)
        interest_rate = 12;
    else if (cibilscore < 690 && cibilscore >= 680)
        interest_rate = 10;
    else if (cibilscore < 700 && cibilscore >= 690)
        interest_rate = 9;
    else if (cibilscore < 710 && cibilscore >= 700)
        interest_rate = 8;
    else
        interest_rate = 7;
    a = pow((1 + (float)interest_rate / 100), tenure / 12);

    EMI = (float)((loan_amount * 100000) * a) / tenure;

    if (income >= 100000 && EMI <= income / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income >= 100000 && EMI > income / 2)
    {
        EMI = income / 2;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 100000 && income >= 60000 && income >= EMI * 5 / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 100000 && income >= 60000 && income < EMI * 5 / 2)
    {
        EMI = income * 2 / 5;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 60000 && income >= 20000 && income >= EMI * 10 / 3)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 60000 && income >= 20000 && income < EMI * 10 / 3)
    {
        EMI = income * 3 / 10;
        approved_loan = (float)EMI * tenure / a;
    }
}

void interest_personal(int cibilscore, int loan_amount, int tenure, float income)
{
    float a;

    if (cibilscore < 630 && cibilscore >= 620)
        interest_rate = 18;
    else if (cibilscore < 640 && cibilscore >= 630)
        interest_rate = 16;
    else if (cibilscore < 650 && cibilscore >= 640)
        interest_rate = 15;
    else if (cibilscore < 670 && cibilscore >= 650)
        interest_rate = 13;
    else if (cibilscore < 690 && cibilscore >= 670)
        interest_rate = 12;
    else if (cibilscore < 700 && cibilscore >= 690)
        interest_rate = 11;
    else
        interest_rate = 10;
    a = pow((1 + (float)interest_rate / 100), tenure / 12);

    EMI = (float)((loan_amount * 100000) * a) / tenure;

    if (income >= 100000 && EMI <= income / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income >= 100000 && EMI > income / 2)
    {
        EMI = income / 2;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 100000 && income >= 60000 && income >= EMI * 5 / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 100000 && income >= 60000 && income < EMI * 5 / 2)
    {
        EMI = income * 2 / 5;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 60000 && income >= 20000 && income >= EMI * 10 / 3)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 60000 && income >= 20000 && income < EMI * 10 / 3)
    {
        EMI = income * 3 / 10;
        approved_loan = (float)EMI * tenure / a;
    }
}

void interest_business(int cibilscore, int loan_amount, int tenure, float income)
{
    float a;

    if (cibilscore < 720 && cibilscore >= 700)
        interest_rate = 24;
    else if (cibilscore < 730 && cibilscore >= 720)
        interest_rate = 20;
    else if (cibilscore < 740 && cibilscore >= 730)
        interest_rate = 18;
    else if (cibilscore < 760 && cibilscore >= 740)
        interest_rate = 15;
    else if (cibilscore < 770 && cibilscore >= 760)
        interest_rate = 14;
    else
        interest_rate = 13;
    a = pow((1 + (float)interest_rate / 100), tenure / 12);

    EMI = (float)((loan_amount * 100000) * a) / tenure;

    if (income >= 100000 && EMI <= income / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income >= 100000 && EMI > income / 2)
    {
        EMI = income / 2;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 100000 && income >= 60000 && income >= EMI * 5 / 2)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 100000 && income >= 60000 && income < EMI * 5 / 2)
    {
        EMI = income * 2 / 5;
        approved_loan = (float)EMI * tenure / a;
    }

    else if (income < 60000 && income >= 20000 && income >= EMI * 10 / 3)
        approved_loan = (float)loan_amount * 100000;
    else if (income < 60000 && income >= 20000 && income < EMI * 10 / 3)
    {
        EMI = income * 3 / 10;
        approved_loan = (float)EMI * tenure / a;
    }
}

void final_output(int interest_rate, float approved_loan, int tenure, float EMI)
{
    show_processing2();
    printf("\n________________________________________________________________________________________________\n");
    printf("Congratulations!\nYour loan has been approved.\n");
    printf("Loan details:\n");
    printf("1. Loan amount= Rs.%.2f\n2. Tenure= %d months\n3. Rate of interest= %d\n4. EMI= Rs.%.2f\n", approved_loan, tenure, interest_rate, EMI);
    printf("Thank you.");
}

void show_loading()
{
    const char chars[] = {'|', '/', '-', '\\'};
    const int numChars = sizeof(chars) / sizeof(chars[0]);
    const int numIterations = 20;

    for (int i = 0; i < numIterations; ++i)
    {
        printf("Loading... %c\r", chars[i % numChars]);
        fflush(stdout);
        Sleep(100); // Sleep for 100,000 microseconds (0.1 seconds)
    }
    for (int j = 0; j < 14; j++)
    { // Adjust the number of spaces as needed
        printf(" ");
    }
    printf("\n");

    fflush(stdout);
}

void show_processing2()
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

void show_wait()
{
    int i;
    int n = 4; // Number of iterations or steps in your task

    printf("Please wait ");

    for (i = 0; i < n; i++)
    {
        printf(".");
        fflush(stdout); // Flush the output to the console
        Sleep(500);     // Sleep for 1 second
    }
    printf("\r");
    for (int j = 0; j < 20; j++)
    { // Adjust the number of spaces as needed
        printf(" \x1b[0m");
    }
    printf("\n");

    fflush(stdout);
}
