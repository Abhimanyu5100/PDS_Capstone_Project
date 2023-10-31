#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <conio.h>
int insurance();
int insurance_driver();

int policyNum()
{
    int policyNum = rand() % 9000000000 + 1000000000;
    return policyNum;
}
void Show_load()
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
int insurance_driver()
{
    insurance();
}

int insurance()
{
    FILE *file1 = fopen("life_insurance.txt", "a");
    if (file1 == NULL)
    {
        printf("Error opening file for writing.\n");
        return 1;
    }
    FILE *file2 = fopen("health_insurance.txt", "a");
    if (file2 == NULL)
    {
        printf("Error opening file for writing.\n");
        return 1;
    }
    FILE *file3 = fopen("car_insurance.txt", "a");
    if (file3 == NULL)
    {
        printf("Error opening file for writing.\n");
        return 1;
    }

    int value, value_car, cover_value, n, a, d, c, z, k, age_car, ag, year, p, acc_num, premium0, cover, age, cover_amnt, cover_amn, alco;
    float premium;
    char ph[100], benefi[100];
    srand(time(NULL));
start:
    printf("\nChoose from the following options");
    printf("\n1. Life insurance");
    printf("\n2. Health insurance");
    printf("\n3. Car insurance");
    printf("\nEnter your choice : ");
    scanf("%d", &n);
    getchar();

    switch (n)
    {
    case 1:
    life_insurance:
        printf("\nYou have selected life insurance.\n");
        printf("Enter the name of policy holder: ");
        gets(ph);
        printf("Enter the name of beneficiery: ");
        gets(benefi);
        
    cover:
        printf("\nSelect the coverage for your life insurance");
        printf("\n1. 50 lakhs");
        printf("\n2. 1 crore ");
        printf("\n3. 2 crores");
        printf("\nEnter your choice: ");
        scanf("%d", &cover);
        Show_load();

        if (cover == 1)
        {
            cover_value = 5000000;
            premium0 = 340;
            printf("\nThe premium you have to pay is Rs 340 per month\n");
        select2:
            printf("Enter 1 to continue and 0 to exit: ");
            scanf("%d", &p);
            if (p == 0)
            {
                printf("\nYour insurance application has been cancelled");
                goto start;
                break;
            }
            else if (p == 1)
            {

                Show_load();
                printf("\n----------------------------------------------------------------------");
                printf("\nThe details of your insurance are as follows:");
                printf("\n\nThe name of policy holder: ");
                puts(ph);
                printf("\nName of beneficiery: ");
                puts(benefi);
                printf("\nCoverage amount : 50 lakhs ");
                printf("\n\nyour policy number for insurance is %010d", policyNum());
                printf("\n\nThe premium you have to pay is Rs %d per month", premium0);
                printf("\n\nThank you for taking the insurance");
            }
            else
            {
                printf("\ninvalid input");
                goto select2;
            }
        }

        else if (cover == 2)
        {
            cover_value = 10000000;
            premium0 = 580;
            printf("\nThe premium you have to pay is Rs 580 per month\n");
        select3:
            printf("\nEnter 1 to continue and 0 to exit: ");
            scanf("%d", &p);
            if (p == 0)
            {
                printf("\nYour insurance application has been cancelled.");
                goto start;
                break;
            }
            else if (p == 1)
            {
                Show_load();
                printf("\n----------------------------------------------------------------------");
                printf("\nThe details of your insurance are as follows:");
                printf("\nThe name of policy holder: ");
                puts(ph);
                printf("\nName of beneficiery: ");
                puts(benefi);
                printf("\nCoverage amount : 1 crore ");
                printf("\n\nYour policy number for insurance is %010d", policyNum());
                printf("\n\nThe premium you have to pay is Rs. %d per month", premium0);
                printf("\n\nThank you for taking the insurance. \n");
            }
            else
            {
                printf("\ninvalid input");
                goto select3;
            }
        }

        else if (cover == 3)
        {
            cover_value = 20000000;
            premium0 = 920;
            printf("\nThe premium you have to pay is Rs 920 per month");
        select4:
            printf("\nEnter 1 to continue and 0 to exit");
            scanf("%d", &p);
            if (p == 0)
            {
                printf("\nYour insurance application has been cancelled");
                goto start;
                break;
            }
            else if (p == 1)
            {
                Show_load();
                printf("\n----------------------------------------------------------------------");
                printf("\nThe details of your insurance is as follows");
                printf("\n");
                printf("\nThe name of policy holder: ");
                puts(ph);
                printf("\nName of beneficiery: ");
                puts(benefi);
                printf("\nCoverage amount : 2 crores ");
                printf("\n\nyour policy number for insurance is %010d", policyNum());
                printf("\n\nThe premium you have to pay is Rs %d per month", premium0);
                printf("\n\nThank you for taking the insurance .");
            }
            else
            {
                printf("\ninvalid input");
                goto select4;
            }
        }
        else
        {
            Show_load();

            printf("invalid input, please enter from 1 to 3 ");
            goto cover;
        }

        fprintf(file1, "\n----------------------------------------------------------------------");
        fprintf(file1, "\n\nThe name of policy holder: %s", ph);
        fprintf(file1, "\nName of beneficiery: %s", benefi);
        fprintf(file1, "\nCoverage amount :%d", cover_value);
        fprintf(file1, "\n\nyour policy number for life insurance is %010d", policyNum());
        fprintf(file1, "\n\nThe premium you have to pay is Rs %d per month", premium0);

        fclose(file1);

        break;
    case 2:
        printf("\nYou have selected health insurance");
        printf("\n");
        printf("\nEnter the name of beneficiery: ");
        gets(benefi);
        
    age:
        printf("\nEnter your age: ");
        scanf("%d", &age);
        if (age < 18)
        {
            printf("\nyou cannot apply for insurance");
            goto age;
        }
    alcohol:
        printf("\nEnter 1 if you smoke or consume alcohol else enter 0 \n Your choice: ");
        scanf("%d", &alco);
        if (alco != 1 && alco != 0)
        {
            printf("invalid input");
            goto alcohol;
        }
    health:
        printf("\nThe coverage amounts available for you are as follows");
        printf("\n1. 2,00,000\n2. 5,00,000\n3. 10,00,000\n4. 20,00,000");
        printf("\n\nChoose your desired coverage amount: ");
        scanf("%d", &cover_amnt);
        if (cover_amnt == 1 || cover_amnt == 200000)
        {
            cover_amn = 200000;
            if (age > 60)
            {
                if (alco == 1)
                {
                    d = 680;

                    printf("\nThe premium for selected coverage is rs 680 per month for next 5 years");
                approve15:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person : %d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 680 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve15;
                    }
                }
                else
                {
                    d = 540;
                    printf("\nThe premium applied for selected coverage is Rs 540 per month for next 5 years");
                approve14:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 540 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\nInvalid input");
                        goto approve14;
                    }
                }
            }
            else if (age >= 18 && age <= 60)
            {
                if (alco == 1)
                {
                    d = 580;
                    printf("\nThe premium for selected coverage is rs 580 per month for next 5 years");
                approve13:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\n Your application has been cancelled");
                        goto start;
                        ;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your health insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 580 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve13;
                    }
                }
                else
                {
                    d = 470;
                    printf("\nThe premium applied for selected coverage is Rs 470 per month for next 5 years");
                approve12:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amnt);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 470 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve12;
                    }
                }
            }
        }
        else if (cover_amnt == 2 || cover_amnt == 500000)
        {
            cover_amn = 500000;
            if (age > 60)
            {
                if (alco == 1)
                {
                    d = 880;

                    printf("\nThe premium for selected coverage is rs 880 per month for next 5 years");
                approve11:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 880 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve11;
                    }
                }
                else
                {
                    d = 740;

                    printf("\nThe premium applied for selected coverage is Rs 740 per month for next 5 years");
                approve10:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 740 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve10;
                    }
                }
            }
            else if (age >= 18 && age <= 60)
            {
                if (alco == 1)
                {
                    d = 680;
                    printf("\nThe premium for selected coverage is rs 680 per month for next 5 years");
                approve9:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 680 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve9;
                    }
                }
                else
                {
                    d = 570;
                    printf("\nThe premium applied for selected coverage is Rs 570 per month for next 5 years");
                approve8:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 570 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve8;
                    }
                }
            }
        }
        else if (cover_amnt == 1000000 || cover_amnt == 3)
        {
            cover_amn = 1000000;
            if (age > 60)
            {
                if (alco == 1)
                {
                    d = 980;
                    printf("\nThe premium for selected coverage is rs 980 per month for next 5 years");
                approve7:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 980 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve7;
                    }
                }
                else
                {
                    d = 740;
                    printf("\nThe premium applied for selected coverage is Rs 740 per month for next 5 years");
                approve6:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your health insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 740 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\nInvalid input");
                        goto approve6;
                    }
                }
            }
            else if (age >= 18 && age <= 60)
            {
                if (alco == 1)
                {
                    d = 780;
                    printf("\nThe premium for selected coverage is rs 780 per month for next 5 years");
                approve5:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;

                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 780 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve5;
                    }
                }
                else
                {
                    d = 570;
                    printf("\nThe premium applied for selected coverage is Rs 570 per month for next 5 years");
                approve4:
                    printf("\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 570 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve4;
                    }
                }
            }
        }
        else if (cover_amnt == 2000000 || cover_amnt == 4)
        {
            cover_amn = 2000000;
            if (age > 60)
            {
                if (alco == 1)
                {
                    d = 1180;
                    printf("\nThe premium for selected coverage is rs 1180 per month for next 5 years");
                approve3:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 1180 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\ninvalid input");
                        goto approve3;
                    }
                }
                else
                {
                    d = 940;
                    printf("\nThe premium applied for selected coverage is Rs 940 per month for next 5 years");
                approve2:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 940 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\nInvalid input");
                        goto approve2;
                    }
                }
            }
            else if (age >= 18 && age <= 60)
            {
                if (alco == 1)
                {
                    d = 980;
                    printf("\nThe premium for selected coverage is rs 980 per month for next 5 years");
                approve1:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k == 1)
                    {
                        Show_load();
                        printf("\nThe details of your insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 980 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("\nInvalid input");
                        goto approve1;
                    }
                }
                else if (alco == 0)
                {
                    d = 770;
                    printf("\nThe premium applied for selected coverage is Rs 770 per month for next 5 years");
                approve:
                    printf("\n\nEnter 1 to continue with the insurance and enter 0 to cancel the application: ");
                    scanf("%d", &k);

                    if (k == 0)
                    {
                        Show_load();
                        printf("\n\nYour application has been cancelled");
                        goto start;
                        break;
                    }
                    else if (k = 1)
                    {
                        Show_load();
                        printf("\nThe details of your health insurance is as follows");
                        printf("\n");
                        printf("\nName of beneficiery: ");
                        puts(benefi);
                        printf("\nAge of person :%d ", age);
                        printf("\n\nThe plan that you have selected is %d", cover_amn);
                        printf("\n\nyour policy number for insurance is %010d", policyNum());
                        printf("\n\nThe premium for selected coverage is rs 770 per month for next 5 years");
                        printf("\n\nThank you for taking the insurance");
                    }
                    else
                    {
                        printf("invalid input");

                        goto approve;
                    }
                }
            }
        }
        else
        {
            printf("\ninvalid input , Enter amount from above list  ");
            goto health;
        }
        fprintf(file2, "\n--------------------------------------------------------------------");
        fprintf(file2, "\n\nThe details of your health insurance is as follows");
        fprintf(file2, "\n\nName of beneficiery:%s ", benefi);
        fprintf(file2, "\n\nAge of person :%d ", age);
        fprintf(file2, "\n\nThe plan that you have selected is %d", cover_amn);
        fprintf(file2, "\n\nyour policy number for insurance is %010d", policyNum());
        fprintf(file2, "\n\nThe premium for selected coverage is rs %d per month for next 5 years", d);
        fclose(file2);

        break;

    case 3:

        printf("\nYou have  selected auto insurance");
        printf("\n\nEnter your name: ");
        gets(benefi);
        printf("\n\nEnter your age in years: ");
        scanf("%d", &ag);
        if (ag < 18)
        {
            printf("\nyou are not eligible to apply for insurance");
        }
        else if (ag >= 18 && ag <= 80)
        {

            printf("\n\nEnter the value of your car in Rs : ");
            scanf("%d", &value_car);
            printf("\n\nEnter the age of your car in years: ");
            scanf("%d", &age_car);
            if (age_car > 0 && age_car <= 5)
            {
                premium = (value_car * 2.3625) / 100;
                printf("\n\nThe premium amount for your car is Rs %f per year", premium);
                printf("\n\nThe insurance will cover all the accidental damage to the car and repairing of any car body part including engine");
            select:
                printf("\nenter 1 to continue and 0 to exit");
                scanf("%d", &z);
                if (z != 1 && z != 0)
                {
                    printf("\ninvalid input");
                    goto select;
                }
                else if (z == 0)
                    break;
                else
                {
                    Show_load();
                    printf("\n\nCongratulations, your insurance has been done");
                    printf("\n\nThe details are as follows");
                    printf("\n\nName of policy holder: ");
                    puts(benefi);
                    printf("\nAge of owner is %d", ag);
                    printf("\n\nyour policy number for insurance is %010d", policyNum());
                    printf("\n\nValue of car is Rs %d", value_car);
                    printf("\n\nAge of car is %d years", age_car);
                    printf("\n\nThank you ");
                }
            }
            else if (age_car > 5 && age_car <= 15)
            {
                premium = (value_car * 3.3625) / 100;
                printf("\n\nThe premium amount for your car is Rs %f per year", premium);
                printf("\n\nThe insurance will cover all the accidental damage to the car and repairing of any car body part including engine");
            select1:
                printf("\nenter 1 to continue and 0 to exit");
                scanf("%d", &z);
                if (z != 1 && z != 0)
                {
                    printf("\ninvalid input");
                    goto select1;
                }
                else if (z == 0)
                    break;
                else
                {
                    Show_load();

                    printf("\n\nCongratulations, your insurance has been done");
                    printf("\n\nThe details are as follows");
                    printf("\n\nName of policy holder: ");
                    puts(benefi);
                    printf("\nAge of owner is %d", ag);
                    printf("\n\nyour policy number for insurance is %010d", policyNum());
                    printf("\n\nValue of car is Rs %d", value_car);
                    printf("\n\nAge of car is %d years", age_car);
                    printf("\n\nThank you ");
                }
            }
            else
            {
                printf("\n\nFor this age of car you cannot apply for insurance");
            }
        }
        else
        {
            printf("\ninvalid inpur");
        }
        fprintf(file3, "\n---------------------------------------------------------------------------");
        fprintf(file3, "\n\nThe premium amount for your car is Rs %f per year", premium);
        fprintf(file3, "\n\nThe insurance will cover all the accidental damage to the car and repairing of any car body part including engine");
        fprintf(file3, "\n\nThe details are as follows");
        fprintf(file3, "\n\nName of policy holder %s", benefi);
        fprintf(file3, "\nAge of owner is  %d", ag);
        fprintf(file3, "\n\nyour policy number for car insurance is %010d", policyNum());
        fprintf(file3, "\n\nValue of car is Rs %d", value_car);
        fprintf(file3, "\n\nAge of car is %d years", age_car);
        fclose(file3);

        break;

    default:
        printf("invalid input");
        goto start;
    }
    return 0;
}