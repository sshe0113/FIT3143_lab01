#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    printf("Hello from AWS ParallelCluster!\n");
    printf("Running on: %s\n", hostname);

    return 0;
}