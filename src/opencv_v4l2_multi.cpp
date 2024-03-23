/*
 * opencv_v4l2 - opencv_v4l2.cpp file
 *
 * Copyright (c) 2017-2018, e-con Systems India Pvt. Ltd.  All rights reserved.
 *
 */

#include <opencv2/opencv.hpp>
#include <iostream>
#include <sys/time.h>
#include <cstdlib>
#include "v4l2_helper.h"

using namespace std;
using namespace cv;

unsigned int GetTickCount()
{
        struct timeval tv;
        if(gettimeofday(&tv, NULL) != 0)
                return 0;
 
        return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

void init_cam(int width, int height) {
	const char *videodev0, *videodev1, *videodev2, *videodev3, *videodev4, *videodev5;
	videodev0 = "/dev/video0";
	videodev1 = "/dev/video1";
	videodev2 = "/dev/video2";
	videodev3 = "/dev/video3";
	videodev4 = "/dev/video4";
	videodev5 = "/dev/video5";

	if (helper_init_cam(videodev0, width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR) < 0) {
		cout << "video0 not initialized properly" << endl;
		return EXIT_FAILURE;
	}
	if (helper_init_cam(videodev1, width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR) < 0) {
		cout << "video1 not initialized properly" << endl;
		return EXIT_FAILURE;
	}
	if (helper_init_cam(videodev2, width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR) < 0) {
		cout << "video2 not initialized properly" << endl;
		return EXIT_FAILURE;
	}
	if (helper_init_cam(videodev3, width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR) < 0) {
		cout << "video3 not initialized properly" << endl;
		return EXIT_FAILURE;
	}
	if (helper_init_cam(videodev4, width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR) < 0) {
		cout << "video4 not initialized properly" << endl;
		return EXIT_FAILURE;
	}
	if (helper_init_cam(videodev5, width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR) < 0) {
		cout << "video5 not initialized properly" << endl;
		return EXIT_FAILURE;
	}
}

/*
 * Other formats: To use pixel formats other than UYVY, see related comments (comments with
 * prefix 'Other formats') in corresponding places.
 */
int main(int argc, char **argv)
{
	unsigned int width, height;
	unsigned int start, end, fps = 0;
	unsigned char* ptr_cam_frame;
	int bytes_used;

	Mat yuyv_frame, preview;
#if defined(ENABLE_DISPLAY) && defined(ENABLE_GL_DISPLAY) && defined(ENABLE_GPU_UPLOAD)
	cuda::GpuMat gpu_frame;
#endif

	if (argc == 3) {
		string width_str = argv[1];
		string height_str = argv[2];
		try {
			size_t pos;
			width = stoi(width_str, &pos);
			if (pos < width_str.size()) {
				cerr << "Trailing characters after width: " << width_str << '\n';
			}

			height = stoi(height_str, &pos);
			if (pos < height_str.size()) {
				cerr << "Trailing characters after height: " << height_str << '\n';
			}
		} catch (invalid_argument const &ex) {
			cerr << "Invalid width or height\n";
			return EXIT_FAILURE;
		} catch (out_of_range const &ex) {
			cerr << "Width or Height out of range\n";
			return EXIT_FAILURE;
		}
	} else {
		cout << "Note: This program accepts (only) three arguments.\n";
		cout << "First arg: device file path, Second arg: width, Third arg: height\n";
		cout << "No arguments given. Assuming default values. Width: 640; Height: 480\n";
		width = 640;
		height = 480;
	}
	
	init_cam(width, height);
	cout << "Initialized Cameras 0~5" << endl;

#ifdef ENABLE_DISPLAY
	/*
	 * Using a window with OpenGL support to display the frames improves the performance
	 * a lot. It is essential for achieving better performance.
	 */
	#ifdef ENABLE_GL_DISPLAY
	namedWindow("OpenCV V4L2", 4096);
	#else
	namedWindow("OpenCV V4L2");
	#endif
	cout << "Note: Click 'Esc' key to exit the window.\n";
#endif

	/*
	 * 1. As we re-use the matrix across loops for increased performance in case of higher resolutions
	 *    we construct it with the common parameters: rows (height), columns (width), type of data in
	 *    matrix.
	 *
	 *    Re-using the matrix is possible as the resolution of the frame doesn't change dynamically
	 *    in the middle of obtaining frames from the camera. If resolutions change in the middle we
	 *    would have to re-construct the matrix accordingly.
	 *
	 * 2. Other formats: To use formats other than UYVY the 3rd parameter must be modified accordingly
	 *    to pass a valid OpenCV array type for the new pixelformat[2].
	 *
	 * [2]: https://docs.opencv.org/3.4.2/d3/d63/classcv_1_1Mat.html#a2ec3402f7d165ca34c7fd6e8498a62ca
	 */
	yuyv_frame = Mat(height, width, CV_8UC2);
	start = GetTickCount();
	while(1) {
		/*
		 * Helper function to access camera data
		 */
		if (helper_get_cam_frame(&ptr_cam_frame, &bytes_used) < 0) {
			break;
		}

		/*
		 * It's easy to re-use the matrix for our case (V4L2 user pointer) by changing the
		 * member 'data' to point to the data obtained from the V4L2 helper.
		 */
		yuyv_frame.data = ptr_cam_frame;
		if(yuyv_frame.empty()) {
			cout << "Img load failed" << endl;
			break;
		}

		/*
		 * 1. We do not use the cv::cuda::cvtColor (along with cv::cuda::GpuMat matrices) for color
		 *    space conversion as cv::cuda::cvtColor does not support color space conversion from
		 *    UYVY to BGR (at least in OpenCV 3.3.1 and OpenCV 3.4.2).
		 *
		 *    The performance might differ for higher resolutions if it did support the color
		 *    conversion.
		 *
		 * 2. Other formats: To use formats other than UYVY, the third parameter of cv::cvtColor must
		 *    be modified to the corresponding color converison code[3].
		 *
		 * [3]: https://docs.opencv.org/3.4.2/d7/d1b/group__imgproc__misc.html#ga4e0972be5de079fed4e3a10e24ef5ef0
		 */
		cvtColor(yuyv_frame, preview, COLOR_YUV2BGR_UYVY);

#ifdef ENABLE_DISPLAY
	/*
	 * It is possible to use a GpuMat for display (imshow) only
	 * when window is created with OpenGL support. So,
	 * ENABLE_GL_DISPLAY is also required for gpu_frame.
	 *
	 * Ref: https://docs.opencv.org/3.4.2/d7/dfc/group__highgui.html#ga453d42fe4cb60e5723281a89973ee563
	 */
	#if (defined ENABLE_GL_DISPLAY) && (defined ENABLE_GPU_UPLOAD)
		/*
		 * Uploading the frame matrix to a cv::cuda::GpuMat and using it to display (via cv::imshow) also
		 * contributes to better and consistent performance.
		 */
		gpu_frame.upload(preview);
		imshow("OpenCV V4L2", gpu_frame);
	#else
		imshow("OpenCV V4L2", preview);
	#endif
#endif

		/*
		 * Helper function to release camera data. This must be called for every
		 * call to helper_get_cam_frame()
		 */
		if (helper_release_cam_frame() < 0)
		{
			break;
		}

#ifdef ENABLE_DISPLAY
		if(waitKey(1) == 27) break;
#endif

		fps++;
		end = GetTickCount();
		if ((end - start) >= 1000) {
			cout << "fps = " << fps << endl ;
			fps = 0;
			start = end;
		}

		/*
		 * Releasing the Matrix is not required.
		 *
		 * 1. release() is called by default in the destructor.
		 * 2. The cv::Mat documentation says:
		 * "If the matrix header points to an external data set (see Mat::Mat),
		 * the reference counter is NULL, and the method has no effect in this case."
		 * yuyv_frame.release();
		 */
	}

	/*
	 * Helper function to free allocated resources and close the camera device.
	 */
	if (helper_deinit_cam() < 0)
	{
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
