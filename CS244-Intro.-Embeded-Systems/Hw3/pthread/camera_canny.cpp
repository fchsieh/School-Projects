/*
*/
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <unistd.h>
#include "opencv2/opencv.hpp"
#include "canny_util.h"

using namespace std;
using namespace cv;

/* Possible options: 320x240, 640x480, 1024x768, 1280x1040, and so on. */
/* Pi Camera MAX resolution: 2592x1944 */

#define WIDTH 640
#define HEIGHT 480
#define NFRAME 1.0

double get_wall_time()
{
    struct timeval time;
    if (gettimeofday(&time, NULL))
    {
        //  Handle error
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

int main(int argc, char **argv)
{
    char *dirfilename;       /* Name of the output gradient direction image */
    char outfilename[128];   /* Name of the output "edge" image */
    char composedfname[128]; /* Name of the output "direction" image */
    unsigned char *image;    /* The input image */
    unsigned char *edge;     /* The output edge image */
    int rows, cols;          /* The dimensions of the image. */
    float sigma,             /* Standard deviation of the gaussian kernel. */
        tlow,                /* Fraction of the high threshold in hysteresis. */
        thigh;               /* High hysteresis threshold control. The actual
			        threshold is the (100 * thigh) percentage point
			        in the histogram of the magnitude of the
			        gradient image that passes non-maximal
			        suppression. */

    /****************************************************************************
    * Get the command line arguments.
    ****************************************************************************/
    if (argc < 4)
    {
        fprintf(stderr, "\n<USAGE> %s sigma tlow thigh [writedirim]\n", argv[0]);
        fprintf(stderr, "      sigma:      Standard deviation of the gaussian");
        fprintf(stderr, " blur kernel.\n");
        fprintf(stderr, "      tlow:       Fraction (0.0-1.0) of the high ");
        fprintf(stderr, "edge strength threshold.\n");
        fprintf(stderr, "      thigh:      Fraction (0.0-1.0) of the distribution");
        fprintf(stderr, " of non-zero edge\n                  strengths for ");
        fprintf(stderr, "hysteresis. The fraction is used to compute\n");
        fprintf(stderr, "                  the high edge strength threshold.\n");
        fprintf(stderr, "      writedirim: Optional argument to output ");
        fprintf(stderr, "a floating point");
        fprintf(stderr, " direction image.\n\n");
        exit(1);
    }

    sigma = atof(argv[1]);
    tlow = atof(argv[2]);
    thigh = atof(argv[3]);
    rows = HEIGHT;
    cols = WIDTH;

    if (argc == 5)
        dirfilename = (char *)"dummy";
    else
        dirfilename = NULL;

    VideoCapture cap;
    // open the default camera (/dev/video0)
    // Check VideoCapture documentation for more details
    if (!cap.open(0))
    {
        cout << "Failed to open /dev/video0" << endl;
        return 0;
    }
    cap.set(CV_CAP_PROP_FRAME_WIDTH, WIDTH);
    cap.set(CV_CAP_PROP_FRAME_HEIGHT, HEIGHT);

    Mat frame, grayframe;
    int frame_num = 0;
    printf("[INFO] (On the pop-up window) Press ESC to stop the program.\n");

    double t_start, t_begin, t_mid, t_end;
    double sum_process = 0;

    t_start = get_wall_time();

    while (1)
    {
        frame_num++;
        t_begin = get_wall_time();

        //capture
        cap >> frame;
        if (frame.empty())
            break;
        imshow("[RAW] this is you, smile! :)", frame);

        t_mid = get_wall_time();
        cvtColor(frame, grayframe, CV_BGR2GRAY);
        image = grayframe.data;

        /****************************************************************************
        * Perform the edge detection. All of the work takes place here.
        ****************************************************************************/
        if (VERBOSE)
            printf("Starting Canny edge detection.\n");
        if (dirfilename != NULL)
        {
            sprintf(composedfname, "camera_s_%3.2f_l_%3.2f_h_%3.2f_frame%03d.fim",
                    sigma, tlow, thigh, frame_num);
            dirfilename = composedfname;
        }
        canny(image, rows, cols, sigma, tlow, thigh, &edge, dirfilename);

        /****************************************************************************
        * Write out the edge image to a file.
        ****************************************************************************/
        sprintf(outfilename, "camera_s_%3.2f_l_%3.2f_h_%3.2f_frame%03d.pgm", sigma, tlow, thigh, frame_num);
        if (VERBOSE)
            printf("Writing the edge iname in the file %s.\n", outfilename);
        if (write_pgm_image(outfilename, edge, rows, cols, NULL, 255) == 0)
        {
            fprintf(stderr, "Error writing the edge image, %s.\n", outfilename);
            exit(1);
        }
        t_end = get_wall_time();
        double time_elapsed = t_end - t_begin;
        double time_capture = t_mid - t_begin;
        double time_process = t_end - t_mid;

        sum_process += time_process;

        double exec_time = t_end - t_start;

        imshow("[GRAYSCALE] this is you, smile! :)", grayframe);

        printf("Frame #%03d Elapsed time for capturing+processing: %lf + %lf => %lf s\n", frame_num, time_capture, time_process, time_elapsed);

        grayframe.data = edge;
        imshow("[EDGE] this is you, smile! :)", grayframe);

        if (waitKey(10) == 27)
        {
            printf("FPS: %lf, Execution time: %lf, Avg processing time: %lf, Frame#: %03d\n", frame_num / exec_time, exec_time, sum_process / frame_num, frame_num);
            break;
        }
    }

    //free resrources
    grayframe.release();
    delete image;
    return 0;
}
