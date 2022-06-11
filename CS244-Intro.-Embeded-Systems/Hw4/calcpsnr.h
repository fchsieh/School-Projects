#ifndef CALCPSNR_H
#define CALCPSNR_H

#ifdef __cpluplus
extern "C" {
#endif

/* Colleen Murphy
 * CS410 Multimedia Networking
 * Project 2
 * November 8, 2013
 * https://github.com/cmurphy/calcpsnr
 */

#include <math.h>
#include <stdio.h>

double calcpsnr(char* original_file, char* eval_file);
double mse(FILE* orig, FILE* eval, int total);
double psnr(double mse);

#ifdef __cpluplus
}
#endif

#endif  // CALCPSNR_H
