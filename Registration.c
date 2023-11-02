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
    int count = 0, n=1, age, i, input, check, paisa, rows, ask;
    long long int mn;
    char names[50], add[50], pass[15], ch;
    int Account_number;
    FILE *file;

    long long int min = pow(10, 8);
    long long int max = pow(10, 9) - 1;

    // Print a line with increased font size or different color
    printf("\033[1;33m                                          WELCOME TO PLUTUS FINCORP\033[0m\n\n");
start:
    printf("What would you like to do?\n1. Register new account[i].\n2. Login into an existing account[i].\n");

    printf("Your choice: ");

    scanf("%d", &input);

    switch (input)

    {
    case 1:
        file = fopen("file.csv", "a");
        if (file == NULL)
        {
            printf("Error opening the file.\n");
            return 1;
        }

        
        Account account[100];



        for (int i = 0; i < n; i++)
        {
            getchar(); // To consume the newline character left by the previous scanf
            printf("Please enter your name: ");
            fgets(names, sizeof(names), stdin);
            names[strcspn(names, "\n")] = '\0'; // Remove the trailing newline
            strcpy(account[i].name, names);

            printf("Please enter your city: ");
            fgets(add, sizeof(add), stdin);
            add[strcspn(add, "\n")] = '\0';
            strcpy(account[i].address, add);

            printf("Please enter your age in years: "); age:
            scanf("%d", &age);
            if (age >=18 && age<=130)
            {
                account[i].age = age;
            }
            else if (age<18 && age>0)
            {
                printf("You are under 18 years of age and are not eligible to get a bank account[i].");
                return 0;
            }
            else {printf("Invalid input!\nPlease enter a valid age: "); goto age;}

            printf("Enter your Phone number: "); mobo:
            scanf("%lld", &mn);
            account[i].phone_no = mn;
            if (mn< 6*pow(10,10) || mn>=pow(10,11))
            {
                printf("Invalid moible number! Enter your mobile number again: ");
                goto mobo;
            }

            srand(time(NULL));
            Account_number = rand() % (max - min + 1) + min;
            account[i].account_no = Account_number;
            fflush(stdin);
            printf("Input a password(Between 6 - 12 characters): ");
            fgets(pass, sizeof(pass), stdin);
            pass[strcspn(pass, "\n")] = '\0';

            // Copy at most 14 characters to ensure null-termination
            strncpy(account[i].password, pass, sizeof(account[i].password) - 1);
            account[i].password[sizeof(account[i].password) - 1] = '\0';

            showLoading();
            printf("\nAccount created.\n");
            printf ("Please enter an initial deposit amount in rupess: ");
            scanf("%d", &paisa);
            account[i].amount = paisa;

            printf("\n_______________________________________________________________________________________________________\n");
        }

        for (int i = 0; i <n; i++)
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
                printf("Error writing to file.\n");
                return 1;
            }
        }

        fclose(file);

        printf("\nACCOUNT DETAILS:\n\n");

        file = fopen("file.csv", "r");

        if (file == NULL)
        {
            printf("Error opening the file.\n");
            return 1;
        }

        for (int i = 0; i < n ; i++)
        {
            fscanf(file, "%s,%s,%d,%lld,%lld,%s,%d\n",
                    account[i].name,
                   account[i].address,
                   &account[i].age,
                   &account[i].phone_no,
                   &account[i].account_no,
                   account[i].password,
                   &account[i].amount);

            printf("Name: %s\n", account[i].name);
            printf("City: %s\n", account[i].address);
            printf("Age: %d\n", account[i].age);
            printf("Phone Number: %lld\n", account[i].phone_no);
            printf("Account Number(Login ID): %lld\n", account[i].account_no);
            printf("Password : %s\n", account[i].password);
            printf("Current Balance: Rs. %d\n", account[i].amount);
            printf("\n");
            printf("(Please remember the account[i] number and password for future references)\n");
            printf("______________________________________________________________________________________________________\n");
        }

        if (file == NULL) {
        printf("Could not open file %s\n", file);
        return 1; // Exit with an error
    }

    // Read the file character by character
    while ((ch = fgetc(file)) != EOF) {
        // Count newline characters to determine rows
        if (ch == '\n') {
            rows++;
        }
    }

        fclose(file);
        Sleep(3000); goto start;
        break; 
        

    case 2:
    
            showLoading();

            system("cls");

            printf("Welcome to the login page.");

            int file_check=0;
            long long int log;
            char passw[15]; pass:
            printf("\nEnter your LOGIN ID(Account number) : "); 
            id:
            scanf("%lld", &log);
            fflush(stdin);
            printf("\nEnter your Password (It is case sensitive): "); 
            fgets(passw, sizeof(passw), stdin);
            passw[strcspn(passw, "\n")] = '\0';

            file = fopen("file.csv", "r");
            for (int i = 0; i < rows; i++)
            {
                fscanf(file, "%lld,%s\n", &account[i].account_no, account[i].password);
                if (account[i].account_no == log && (strcmp(account[i].password, passw) == 0))
                {
                    file_check++;
                    printf("\nYou have succesfully logged into your account[i].");
                    break;
                }
            }

                 if(file_check == 0)
                {
                    printf("\nYou have entered wrong login details.\nDo you want to exit?\n1. Yes\n2. No\nYour choice: "); choice:
                    scanf("%d", &ask);
                    switch (ask)
                    {
                        case 1:
                        return 0;
                        break;

                        case 2:
                        goto pass;
                        break;

                        default:
                        printf("Enter a valid choice: ");
                        goto choice;
                    }
                }

            fclose(file);

                }
           

    end:
    return 1;
}

void showLoading()
{
    const char chars[] = {'|', '/', '-', '\\'};
    const int numChars = sizeof(chars) / sizeof(chars[0]);
    const int numIterations = 20;

    for (int i = 0; i < numIterations; ++i)
    {
        printf("Loading... %c\r", chars[i % numChars]);
        fflush(stdout);
        Sleep(100); // Sleep for 100 microseconds (0.1 seconds)
    }

    printf("                     \n");
}
