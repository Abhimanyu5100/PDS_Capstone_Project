#include <stdio.h>

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

int main() {
    const char *filename = "file.csv";
    int numRows = countRowsInCSV(filename);

    if (numRows >= 0) {
        printf("The CSV file \"%s\" contains %d rows.\n", filename, numRows);
    } else {
        printf("Failed to count the rows in the CSV file.\n");
    }

    return 0;
}