#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <stdio.h>


using namespace std;
using namespace cv;

#define RTSP_URL "rtsp://admin:admin@192.168.0.130:554"

int main(int argc, const char** argv) {
	VideoCapture cap;
	cap.open(RTSP_URL);

    if(!cap.isOpened()) return -1;

	namedWindow("opencv", 1);

    for (;;) {
        Mat frame;
        cap >> frame;
        imshow("opencv", frame);

        if (waitKey(30) >= 0) break;
    }

    return 0;
}

