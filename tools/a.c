#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *bytesFromFile(const char *path, long long int start, long long int size) {
    FILE *file = fopen(path, "r");
    fseek(file, start, SEEK_SET);
    char *buffer = (char *)malloc(size);
    fread(buffer, size, 1, file);
    fclose(file);
    return buffer;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Usage: ./a <file1> <file2> <start> <size>\n");
        return 1;
    }
    const char *in1 = argv[1];
    const char *in2 = argv[2];
    const char *start = argv[3];
    const char *size = argv[4];
    // const char *out = argv[4];
    long long int istart = atoll(start);
    long long int isize = atoll(size);

    const char *b1 = bytesFromFile(in1, istart, isize);
    const char *b2 = bytesFromFile(in2, istart, isize);
    long long int count = 0;
    for (long long int i = 0; i < isize; i++) {
        if (b1[i] != b2[i]) {
            count++;
        }
    }

    printf("%lld\n", count);
    /*FILE *outfile = fopen(out, "w");
    fwrite(buffer, isize, 1, outfile);
    fclose(outfile);*/

    return 0;
}
