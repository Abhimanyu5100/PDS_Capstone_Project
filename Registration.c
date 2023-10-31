#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <math.h>
#include <windows.h>
#include <time.h>

void showLoading();
int registration_driver();
int countRowsInCSV(const char *filename);


typedef struct
{
    char name[50];
    char address[200];
    int age;
    long long int phone_no;
    long long int account_no;
    char password[15];
    int amount;
} Account;

int registration_driver()
{
    int count = 0, n=1, age, i, input, check, paisa;
    long long int mn;
    char names[50], add[200], pass[15];
    int Account_number;
    FILE *file;

    long long int min = pow(10, 8);
    long long int max = pow(10, 9) - 1;

    // Print a line with increased font size or different color
    printf("\033[1;33m                                          WELCOMME TO PLUTUS FINCORP\033[0m\n\n");
start:
    printf("What would you like to do?\n1. Register new account.\n2. Login into an existing account.\n");

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
         const char *filename = "example.csv";
         int numRows = countRowsInCSV(filename);

        if (numRows >= 0) {
            printf("The CSV file \"%s\" contains %d rows.\n", filename, numRows);
        } else {
            printf("Failed to count the rows in the CSV file.\n");
        }



        for (int i = numRows; i < numRows + 1; i++)
        {
            getchar(); // To consume the newline character left by the previous scanf
            printf("Please enter your name: ");
            fgets(names, sizeof(names), stdin);
            names[strcspn(names, "\n")] = '\0'; // Remove the trailing newline
            strcpy(account[i].name, names);

            printf("Please enter your address: ");
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
                printf("You are under 18 years of age and are not eligible to get a bank account.");
                goto end;
            }
            else {printf("Invalid input!\nPlease enter a valid age: "); goto age;}

            printf("Enter your Phone number: ");
            scanf("%lld", &mn);
            account[i].phone_no = mn;

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

        for (int i = numRows; i < numRows + 1; i++)
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
            printf("Address: %s\n", account[i].address);
            printf("Age: %d\n", account[i].age);
            printf("Phone Number: %lld\n", account[i].phone_no);
            printf("Account Number(Login ID): %lld\n", account[i].account_no);
            printf("Password : %s\n", account[i].password);
            printf("Current Balance: Rs. %d\n", account[i].amount);
            printf("\n");
            printf("(Please remember the account number and password for future references)\n");
            printf("______________________________________________________________________________________________________\n");
        }
        fclose(file);
        Sleep(3000); goto start;
        break; 
        

    case 2:

            showLoading();

            system("cls");

            printf("Welcome to the login page.");

            int log, file_check=0;
            char passw[15]; pass:
            printf("\nEnter your LOGIN ID(Account number) : "); 
            id:
            scanf("%d", &log);
            fflush(stdin);
            printf("\nEnter your Password (It is case sensitive): "); 
            fgets(passw, sizeof(passw), stdin);
            passw[strcspn(passw, "\n")] = '\0';

            file = fopen("file.csv", "r");
            for (int i = 0; i < 50; i++)
            {
                fscanf(file, "%d,%s\n", &account[i].account_no, account[i].password);
                if (account[i].account_no == log && (strcmp(account[i].password, passw) == 0))
                {
                    file_check++;
                    printf("\nYou have succesfully logged into your account.");
                    break;
                }
            }

                 if(file_check == 0)
                {
                    printf("\nYou have entered wrong login details.\n\n ");
                    goto pass;
                }

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
        printf("Loading... %c\r", chars[i % numChars]);
        fflush(stdout);
        Sleep(100); // Sleep for 100 microseconds (0.1 seconds)
    }

    printf("                     \n");
}
int countRowsInCSV(const char *filename) {
    FILE *check = fopen(filename, "r");
    if (check == NULL) {
        printf("Error opening the file.\n");
        return -1; // Error indicator
    }

    int rowCount = 0;
    char ch;
    while ((ch = fgetc(check)) != EOF) {
        if (ch == '\n') {
            rowCount++;
        }
    }

    fclose(check);
    return rowCount;
}
