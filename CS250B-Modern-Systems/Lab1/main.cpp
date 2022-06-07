// Version 0.1: 2020-04-15

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <chrono>
#include <ctime>

#define SUBSTEP 32

inline int idx(int x, int y, int width) { return y * width + x; }

void step_naive(float* temp, float* temp2, float* conduct, int width,
                int height) {
    for (int i = 0; i < height; i++) {
        temp[idx(0, i, width)] = temp[idx(1, i, width)];
        temp[idx(height - 1, i, width)] = temp[idx(height - 2, i, width)];
    }
    for (int i = 0; i < width; i++) {
        temp[idx(i, 0, width)] = temp[idx(i, 1, width)];
        temp[idx(i, width - 1, width)] = temp[idx(i, width - 2, width)];
    }
    for (int i = 1; i < width - 1; i++) {
        for (int j = 1; j < height - 1; j++) {
            temp2[idx(i, j, width)] =
                temp[idx(i, j, width)] +
                ((temp[idx(i - 1, j, width)] - temp[idx(i, j, width)]) *
                     conduct[idx(i - 1, j, width)]

                 + (temp[idx(i + 1, j, width)] - temp[idx(i, j, width)]) *
                       conduct[idx(i + 1, j, width)]

                 + (temp[idx(i, j - 1, width)] - temp[idx(i, j, width)]) *
                       conduct[idx(i, j - 1, width)] +
                 (temp[idx(i, j + 1, width)] - temp[idx(i, j, width)]) *
                     conduct[idx(i, j + 1, width)]

                 ) * 0.2;
        }
    }
    return;
}

extern void step_optimized(float* temp, float* temp2, float* conduct, int width,
                           int height, int threads, int substeps);

void step(float* temp, float* temp2, float* conduct, int width, int height,
          int threads, int substeps, bool naive) {
    if (naive) {
        for (int i = 0; i < substeps; i++) {
            step_naive(temp, temp2, conduct, width, height);
            if (i < substeps - 1) {
                float* t = temp;
                temp = temp2;
                temp2 = t;
            }
        }
    } else
        step_optimized(temp, temp2, conduct, width, height, threads, substeps);
}

int main(int argc, char** argv) {
    printf("usage: %s [steps] [threads] [initial state file] [naive?]\n",
           argv[0]);
    if (SUBSTEP % 2) {
        printf("ERROR: SUBSTEP should be power of 2!\n");
        exit(1);
    }

    int steps = 256;
    if (argc >= 2) steps = atoi(argv[1]);

    int threads = 1;
    if (argc >= 3) threads = atoi(argv[2]);

    const char* filename = "init.dat";
    if (argc >= 4) {
        filename = argv[3];
    }

    bool isnaive = true;
    if (argc >= 5) {
        if (argv[4][0] == 'N' || argv[4][0] == 'n') isnaive = false;
    }

    FILE* fin = fopen(filename, "rb");

    if (!fin) {
        printf("Input file %s not found!\n", filename);
        exit(1);
    }

    int width;
    int height;
    if (!fread(&width, sizeof(int), 1, fin) ||
        !fread(&height, sizeof(int), 1, fin)) {
        printf("Input file read failed\n");
        exit(1);
    }
    printf("Reading data file width, height = %d %d\n", width, height);
    float* temp =
        (float*)aligned_alloc(32, sizeof(float) * width * height * SUBSTEP);
    float* temp2 =
        (float*)aligned_alloc(32, sizeof(float) * width * height * SUBSTEP);
    float* conduct = (float*)aligned_alloc(32, sizeof(float) * width * height);

    if (fread(temp, sizeof(float), width * height, fin) !=
            (size_t)(width * height) ||
        fread(conduct, sizeof(float), width * height, fin) !=
            (size_t)(width * height)) {
        printf("Input file read failed\n");
        exit(1);
    }
    fclose(fin);

    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point now;
    std::chrono::microseconds duration_micro;

    printf("Starting %s stencil calculation for %d steps\n",
           isnaive ? "naive" : "optimized", steps);
    start = std::chrono::high_resolution_clock::now();

    int ii = 0;
    while (ii < steps) {
        step(temp, temp2, conduct, width, height, threads, SUBSTEP, isnaive);
        ii += SUBSTEP;
        printf("\t Step %8d\n", ii);
    }

    now = std::chrono::high_resolution_clock::now();
    duration_micro =
        std::chrono::duration_cast<std::chrono::microseconds>(now - start);
    printf("Done : %f s\n", 0.000001f * duration_micro.count());
    printf("%s stencil calculation done\n", isnaive ? "Naive" : "Optimized");

    FILE* fout = fopen("output.dat", "wb");
    fwrite(&width, sizeof(int), 1, fout);
    fwrite(&height, sizeof(int), 1, fout);
    fwrite(temp, sizeof(float), width * height, fout);
    fwrite(conduct, sizeof(float), width * height, fout);
    fclose(fout);

    printf("All done! Exiting...\n");
}
