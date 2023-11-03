#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <math.h>
#include <windows.h>
#include <time.h>

void showLoading();
int registration_driver();

typedef struct
{
    char name[50];
    char address[50];
    int age;
    long long int phone_no;
    long long int account_no;
    char password[15];
    int amount;
} Account;

int registration_driver()
{
    int count = 0, n = 1, age, i, input, check, paisa, rows, ask;
    long long int mn;
    char names[50], add[50], pass[15], ch;
    int Account_number;
    FILE *file;

    long long int min = pow(10, 8);
    long long int max = pow(10, 9) - 1;

    // Print a line with increased font size or different color
    printf("\x1b[1;33m                                                  WELCOME TO PLUTUS FINCORP\x1b[0m\n\n");
start:
    printf("\x1b[1;36mWhat would you like to do?\n1. Register new account.\n2. Login into an existing account.\n3. Exit\x1b[1;36m\n");

    printf("\x1b[1;34mYour choice:\x1b[0m ");

    scanf("%d", &input);

    switch (input)

    {
    case 1:
        file = fopen("file.csv", "a");
        if (file == NULL)
        {
            printf("\x1b[1;31mError opening the file.\x1b[0m\n");
            return 1;
        }

        Account account[100];

        for (int i = 0; i < n; i++)
        {
            getchar(); // To consume the newline character left by the previous scanf
            printf("\x1b[1;36m\nPlease enter your name:\x1b[0m ");
            fgets(names, sizeof(names), stdin);
            names[strcspn(names, "\n")] = '\0'; // Remove the trailing newline
            strcpy(account[i].name, names);

            printf("\x1b[1;36mPlease enter your city:\x1b[0m ");
            fgets(add, sizeof(add), stdin);
            add[strcspn(add, "\n")] = '\0';
            strcpy(account[i].address, add);

            printf("\x1b[1;36mPlease enter your age in years:\x1b[0m ");
        age:
            scanf("%d", &age);
            if (age >= 18 && age <= 130)
            {
                account[i].age = age;
            }
            else if (age < 18 && age > 0)
            {
                printf("\n\x1b[1;31mYou are under 18 years of age and are not eligible to get a bank account.\x1b[0m");
                return 1;
            }
            else
            {
                printf("\x1b[1;31mInvalid input!\n\x1b[1;36mPlease enter a valid age:\x1b[0m ");
                goto age;
            }

            printf("\x1b[1;36mEnter your Phone number:\x1b[0m ");
        mobo:
            scanf("%lld", &mn);
            account[i].phone_no = mn;
            if (mn < 6 * pow(10, 9) || mn >= pow(10, 10))
            {
                printf("\x1b[1;31mInvalid moible number!\x1b[1;36m \nEnter your mobile number again: ");
                goto mobo;
            }

            srand(time(NULL));
            Account_number = rand() % (max - min + 1) + min;
            account[i].account_no = Account_number;
            fflush(stdin);
            printf("\x1b[1;36mInput a password(Between 6 - 12 characters):\x1b[0m ");
            fgets(pass, sizeof(pass), stdin);
            pass[strcspn(pass, "\n")] = '\0';

            // Copy at most 14 characters to ensure null-termination
            strncpy(account[i].password, pass, sizeof(account[i].password) - 1);
            account[i].password[sizeof(account[i].password) - 1] = '\0';

            showLoading();
            printf("\n\x1b[1;32mAccount created.\x1b[0m\n\n");
            printf("\x1b[1;36mPlease enter an initial deposit amount in rupess:\x1b[0m ");
            scanf("%d", &paisa);
            account[i].amount = paisa;

            printf("\n\x1b[1;32m_______________________________________________________________________________________________________\x1b[0m\n");
        }

        for (int i = 0; i < n; i++)
        {
            fprintf(file, "%s,%s,%d,%lld,%lld,%s,%d\n",
                    account[i].name,
                    account[i].address,
                    account[i].age,
                    account[i].phone_no,
                    account[i].account_no,
                    account[i].password,
                    account[i].amount);

            if (ferror(file))
            {
                printf("\x1b[1;31mError writing to file.\x1b[0m\n");
                return 1;
            }
        }

        fclose(file);

        printf("\n\x1b[1;33mACCOUNT DETAILS:\x1b[0m\n\n");

        file = fopen("file.csv", "r");

        if (file == NULL)
        {
            printf("\x1b[1;31mError opening the file.\x1b[0m\n");
            return 1;
        }

        for (i = 0; i < n; i++)
        {
            fscanf(file, "%d,%s,%s,%lld,%lld,%s,%d\n",
                   &account[i].age,
                   account[i].name,
                   account[i].address,                   
                   &account[i].phone_no,
                   &account[i].account_no,
                   account[i].password,
                   &account[i].amount);

            printf("\x1b[1;36mName: \x1b[1;37m%s\n", account[i].name);
            printf("\x1b[1;36mCity: \x1b[1;37m%s\n", account[i].address);
            printf("\x1b[1;36mAge: \x1b[1;37m%d\n", account[i].age);
            printf("\x1b[1;36mPhone Number: \x1b[1;37m%lld\n", account[i].phone_no);
            printf("\x1b[1;36mAccount Number(Login ID):\x1b[1;37m %lld\n", account[i].account_no);
            printf("\x1b[1;36mPassword : \x1b[1;37m%s\n", account[i].password);
            printf("\x1b[1;36mCurrent Balance:\x1b[1;37m Rs. %d\n", account[i].amount);
            printf("\n");
            printf("\x1b[1;33m(Please remember the account number and password for future references)\x1b[0m\n");
            printf("\x1b[1;32m______________________________________________________________________________________________________\x1b[0m\n");
        }

        if (file == NULL)
        {
            printf("\x1b[1;31mCould not open file %s\x1b[0m\n", file);
            return 1; // Exit with an error
        }

        // Read the file character by character
        while ((ch = fgetc(file)) != EOF)
        {
            // Count newline characters to determine rows
            if (ch == '\n')
            {
                rows++;
            }
        }

        fclose(file);
        Sleep(3000);
        goto start;
        break;

    case 2:

        showLoading();

        system("cls");

        printf("\n\x1b[1;33mWelcome to the login page.\n");

        int file_check = 0;
        long long int log;
        char passw[15];
    pass:
        printf("\n\x1b[1;36mEnter your LOGIN ID(Account number) :\x1b[0m ");
    id:
        scanf("%lld", &log);
        fflush(stdin);
        printf("\n\x1b[1;36mEnter your Password \x1b[1;34m(It is case sensitive): \x1b[0m");
        fgets(passw, sizeof(passw), stdin);
        passw[strcspn(passw, "\n")] = '\0';

        file = fopen("file.csv", "r");
        int i = 0;
        while (fscanf(file, "%lld,%s", &account[i].account_no, account[i].password) != EOF)
        {
            fscanf(file, "%lld,%s\n", &account[i].account_no, account[i].password);
            if (& account[i].account_no == log && (strcmp(account[i].password, passw) == 0))
            {
                file_check++;
                printf("\n\x1b[1;32mYou have succesfully logged into your account.\x1b[0m\n");
                int user_inp;
            go_back:
                printf("\x1b[1;36m\n\nWhat would you like to do?\n");
                printf("1. Check Balance\n2. Deposit\n3. Withdraw\n4. Explore more options\n5. Exit");
                printf("\n\x1b[1;34mYour choice: \x1b[0m");
            re_enter:
                scanf("%d", &user_inp);

                switch (user_inp)
                {
                    int dep, with;
                case 1:
                    printf("\x1b[1;36mThe balace in your account is:\x1b[0m ");
                    printf("%d", account[i].amount);
                    goto go_back;

                case 2:
                    printf("\x1b[1;36mEnter the amount to deposit:\x1b[0m ");
                    scanf("%d", &dep);

                    account[i].amount += dep;
                    printf("\x1b[1;36mThe balace in your account after deposition is:\x1b[0m ");
                    printf("%d", account[i].amount);
                    goto go_back;

                case 3:
                    printf("\x1b[1;36mThe balace in your account is:\x1b[0m ");
                    printf("%d", account[i].amount);
                withdraw:
                    printf("\n\x1b[1;36mEnter the amount to withdraw:\x1b[0m ");
                    scanf("%d", &with);
                    if (with > account[i].amount)
                    {
                        printf("\n\x1b[1;31mYou don't have enough balance in your account.\x1b[0m");
                        goto withdraw;
                    }
                    else
                    {
                        account[i].amount -= with;
                        printf("\x1b[1;36mThe balace after withdrawal in your account is: \x1b[0m");
                        printf("%d", account[i].amount);
                        goto go_back;
                    }

                case 4:
                    return 0;

                case 5:
                    return 1;

                default:
                    printf("\x1b[1;31mInvalid input!\x1b[0m\n");
                    goto go_back;
                }

                break;
            }
            i++;
        }

        if (file_check == 0)
        {
            printf("\n\x1b[1;31mYou have entered wrong login details.\n\x1b[1;36mDo you want to exit?\n1. Yes\n2. No\n\x1b[1;34mYour choice:\x1b[0m ");
        choice:
            scanf("%d", &ask);
            switch (ask)
            {
            case 1:
                return 1;
                break;

            case 2:
                goto pass;
                break;

            default:
                printf("\x1b[1;36mEnter a valid choice:\x1b[0m ");
                goto choice;
            }
        }

        fclose(file);
        break;

    case 3:

        return 1;
        break;

    default:
        printf("\x1b[1;31mInvalid entry!\x1b[0m\n");
        goto start;
    }

end:
    return 0;
}

void showLoading()
{
    const char chars[] = {'|', '/', '-', '\\'};
    const int numChars = sizeof(chars) / sizeof(chars[0]);
    const int numIterations = 20;

    for (int i = 0; i < numIterations; ++i)
    {
        printf("\x1b[1;35mLoading... %c\x1b[0m\r", chars[i % numChars]);
        fflush(stdout);
        Sleep(100); // Sleep for 100 microseconds (0.1 seconds)
    }

    printf("                     \n");
}

