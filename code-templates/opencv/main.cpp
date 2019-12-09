#include <opencv2/opencv.hpp>
#include <iostream>
using namespace cv;

auto main() -> int {
	Mat image = imread("lenna.png", 1);
	if (!image.data) {
		std::cerr << "Cannot load the iamge" << std::endl;
	}

	namedWindow("Original Image", WINDOW_AUTOSIZE);
	imshow("Original Image", image);


	while(waitKey(0) != 32);
	destroyAllWindows();
	return 0;
}
