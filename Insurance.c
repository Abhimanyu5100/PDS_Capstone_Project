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
void show_load()
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
    return 0;
}

int insurance()
{
    FILE *csvfile1 = fopen("life_insurance.csv", "a");
    if (csvfile1 == NULL)
    {
        printf("Error opening file for writing.\n");
        return 1;
    }
    FILE *csvfile2 = fopen("health_insurance.csv", "a");
    if (csvfile2 == NULL)
    {
        printf("Error opening file for writing.\n");
        return 1;
    }
    FILE *csvfile3 = fopen("car_insurance.csv", "a");
    if (csvfile3 == NULL)
    {
        printf("Error opening file for writing.\n");
        return 1;
    }

    int value, value_car, cover_value, n, a, d, c, z, k,b, age_car, ag, year, p, acc_num, premium0, cover, age, cover_amnt, cover_amn, alco;
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
    b=policyNum();

    switch (n)
    {
    case 1:
    life_insurance:
        printf("\nYou have selected life insurance.\n");
    ph:
        printf("\nEnter the name of policy holder: ");
        gets(ph);
        for (int i = 0; i < strlen(ph); i++)
        {
            if ((ph[i] >= 'a' && ph[i] <= 'z') || (ph[i] >= 'A' && ph[i] <= 'Z' || ph[i] == ' '))
                continue;
            else
            {
                printf("\nenter a valid name");
                goto ph;
            }
        }
    benefi:
        printf("\nEnter the name of beneficiery: ");
        gets(benefi);
        for (int j = 0; j < strlen(benefi); j++)
        {
            if ((benefi[j] >= 'a' && benefi[j] <= 'z') || (benefi[j] >= 'A' && benefi[j] <= 'Z' || benefi[j] == ' '))
                continue;
            else
            {
                printf("\nenter a valid name");
                goto benefi;
            }
        }

    cover:
        printf("\nSelect the coverage for your life insurance");
        printf("\n1. 50 lakhs");
        printf("\n2. 1 crore ");
        printf("\n3. 2 crores");
        printf("\nEnter your choice(1 or 2 or 3): ");
        scanf("%d", &cover);
        show_load();

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

                break;
            }
            else if (p == 1)
            {

                show_load();
                printf("\n----------------------------------------------------------------------");
                printf("\nThe details of your insurance are as follows:");
                printf("\n\nThe name of policy holder: ");
                puts(ph);
                printf("\nName of beneficiery: ");
                puts(benefi);
                printf("\nCoverage amount : 50 lakhs ");
                printf("\n\nyour policy number for insurance is %010d", b);
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

                break;
            }
            else if (p == 1)
            {
                show_load();
                printf("\n----------------------------------------------------------------------");
                printf("\nThe details of your insurance are as follows:");
                printf("\nThe name of policy holder: ");
                puts(ph);
                printf("\nName of beneficiery: ");
                puts(benefi);
                printf("\nCoverage amount : 1 crore ");
                printf("\n\nYour policy number for insurance is %010d", b);
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

                break;
            }
            else if (p == 1)
            {
                show_load();
                printf("\n----------------------------------------------------------------------");
                printf("\nThe details of your insurance is as follows");
                printf("\n");
                printf("\nThe name of policy holder: ");
                puts(ph);
                printf("\nName of beneficiery: ");
                puts(benefi);
                printf("\nCoverage amount : 2 crores ");
                printf("\n\nyour policy number for insurance is %010d", b);
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
            // show_load();

            printf("invalid input, please enter from 1 to 3 ");
            goto cover;
        }

       
        fprintf(csvfile1, "\n%s,%s,%d,%010d,%d",ph,benefi,cover_value,b,premium0);
        fclose(csvfile1);
       

        break;
    case 2:
    b=policyNum();
        printf("\nYou have selected health insurance");
        printf("\n");
    benefi1:
        printf("\nEnter the name of beneficiery: ");
        gets(benefi);

        for (int j = 0; j < strlen(benefi); j++)
        {
            if ((benefi[j] >= 'a' && benefi[j] <= 'z') || (benefi[j] >= 'A' && benefi[j] <= 'Z' || benefi[j] == ' '))
                continue;
            else
            {
                printf("\nenter a valid name");
                goto benefi1;
            }
        }

    age:
        printf("\nEnter your age: ");
        scanf("%d", &age);
        if (age >= 0 && age < 18)
        {
            printf("\nYou cannot apply for insurance.");
            return 0;
            break;
        }
        else if (age >= 18)
        {
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person : %d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\n Your application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your health insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amnt);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d",b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d",b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d",b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your health insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d",b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d",b);
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
                            show_load();
                            printf("\nYour application has been cancelled");

                            break;
                        }
                        else if (k == 1)
                        {
                            show_load();
                            printf("\nThe details of your insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d", b);
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
                            show_load();
                            printf("\n\nYour application has been cancelled");

                            break;
                        }
                        else if (k = 1)
                        {
                            show_load();
                            printf("\nThe details of your health insurance is as follows");
                            printf("\n");
                            printf("\nName of beneficiery: ");
                            puts(benefi);
                            printf("\nAge of person :%d ", age);
                            printf("\n\nThe plan that you have selected is %d", cover_amn);
                            printf("\n\nyour policy number for insurance is %010d",b);
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
            fprintf(csvfile2, "\n%s,%d,%d,%010d,%d",benefi,age,cover_amn,b,d);

            break;
        }

    case 3:
        b=policyNum();
        printf("\nYou have  selected auto insurance");
        printf("\n\nEnter your name: ");
        gets(benefi);
        for (int j = 0; j < strlen(benefi); j++)
        {
            if ((benefi[j] >= 'a' && benefi[j] <= 'z') || (benefi[j] >= 'A' && benefi[j] <= 'Z' || benefi[j] == ' '))
                continue;
            else
            {
                printf("\nEnter a valid name");
                goto benefi1;
            }
        }

    age1:
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
                printf("\nEnter 1 to continue and 0 to exit");
                scanf("%d", &z);
                if (z != 1 && z != 0)
                {
                    printf("\nInvalid input");
                    goto select;
                }
                else if (z == 0)
                {
                    printf("Your application has been cancelled");
                    break;
                }
                else
                {
                    show_load();
                    printf("\n\nCongratulations, your insurance has been done");
                    printf("\n\nThe details are as follows");
                    printf("\n\nName of policy holder: ");
                    puts(benefi);
                    printf("\nAge of owner is %d", ag);
                    printf("\n\nyour policy number for insurance is %010d",b);
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
                {
                    printf("Your application has been cancelled");
                    break;
                }
                else
                {
                    show_load();

                    printf("\n\nCongratulations, your insurance has been done");
                    printf("\n\nThe details are as follows");
                    printf("\n\nName of policy holder: ");
                    puts(benefi);
                    printf("\nAge of owner is %d", ag);
                    printf("\n\nyour policy number for insurance is %010d", b);
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
            printf("\ninvalid input");
            goto age1;
        }
        
        fprintf(csvfile1, "\n%s,%d,%d,%d,%010d,%d",benefi,ag,value_car,age_car,b,premium);
        
        fclose(csvfile3);

        break;

    default:
        printf("invalid input");
        goto start;
    }
    return 0;
}
