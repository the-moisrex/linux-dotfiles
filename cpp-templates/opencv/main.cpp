#include <opencv2/opencv.hpp>
#include <iostream>
using namespace cv;

auto main() -> int {
	Mat image = imread('img.jpg', 1);
	if (!image.data) {
		std::cerr << "Cannot load the iamge" << std::endl;
	}

	namedWindow("Original Image", WINDOW_AUTOSIZE);
	imshow("Original Image", image);


	waitKey(0);
	return 0;
}
