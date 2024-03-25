/*
 * opencv_v4l2 - opencv_v4l2.cpp file
 *
 * Copyright (c) 2017-2018, e-con Systems India Pvt. Ltd.  All rights reserved.
 *
 */

#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <sys/time.h>
#include <cstdlib>
// #include "v4l2_helper.h"
#include <v4l2_util.hpp>

using namespace std;
using namespace cv;


int init_cam(int camidx, CamV4L2* device, 
	const char* devname, int width, int height, 
	bool enable_display_) {
	const char *videodev;
	videodev = devname;

	if (device->helper_init_cam(camidx, videodev, 
		width, height, V4L2_PIX_FMT_UYVY, IO_METHOD_USERPTR, enable_display_) < 0) {
		cout << "video #" << camidx << " not initialized properly" << endl;
		return EXIT_FAILURE;
	}
	else {
		return 0;
	}
}

/*
 * Other formats: To use pixel formats other than UYVY, see related comments (comments with
 * prefix 'Other formats') in corresponding places.
 */
int main(int argc, char **argv)
{
	vector<const char*> devname_list;
	devname_list.push_back("/dev/video0");
	// devname_list.push_back("/dev/video1");	// seems to be broken
	devname_list.push_back("/dev/video2");
	devname_list.push_back("/dev/video3");
	devname_list.push_back("/dev/video4");
	devname_list.push_back("/dev/video5");

	bool enable_display = false;	
	int N;
	unsigned int width, height;

#ifdef ENABLE_DISPLAY
	enable_display = true;
#endif

// #if defined(ENABLE_DISPLAY) && defined(ENABLE_GL_DISPLAY) && defined(ENABLE_GPU_UPLOAD)
// 	vector<cuda::GpuMat> gpu_frame(N);
// #endif

	if (argc == 4) {
		string N_str = argv[1];
		string width_str = argv[2];
		string height_str = argv[3];
		try {
			size_t pos;
			N = stoi(N_str, &pos);
			if (pos < N_str.size()) {
				cerr << "Trailing characters after N: " << N_str << '\n';
			}

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
		N = 1;
		width = 640;
		height = 480;
	}

	vector<CamV4L2> multicam;
	multicam.resize(N);
	for (int idx = 0; idx < N; idx++) {
		init_cam(idx, &multicam.at(idx), devname_list.at(idx), width, height, enable_display);
	}

	cout << "Initialized Cameras 0~5" << endl;

// #ifdef ENABLE_DISPLAY
// 	/*
// 	 * Using a window with OpenGL support to display the frames improves the performance
// 	 * a lot. It is essential for achieving better performance.
// 	 */
// 	#ifdef ENABLE_GL_DISPLAY
// 	for (int idx = 0; idx < N; idx++) {
// 		namedWindow(window_name.append(to_string(idx)), 4096);
// 	}
// 	#else
// 	for (int idx = 0; idx < N; idx++) {
// 		namedWindow(window_name.append(to_string(idx)));
// 	}
// 	#endif
// 	cout << "Note: Click 'Esc' key to exit the window.\n";
// #endif

	for (int idx = 0; idx < N; idx++) {
		multicam.at(idx).running = true;
		// multicam.at(idx).run_thread();
		multicam.at(idx).start_thread();
	}

	while(waitKey(1) != 27) {

// #ifdef ENABLE_DISPLAY
// 	#if (defined ENABLE_GL_DISPLAY) && (defined ENABLE_GPU_UPLOAD)
// 		/*
// 		 * Uploading the frame matrix to a cv::cuda::GpuMat and using it to display (via cv::imshow) also
// 		 * contributes to better and consistent performance.
// 		 */
// 		for (int idx = 0; idx < N; idx++) {
// 			gpu_frame.at(idx).upload(multicam.at(idx).preview);
// 			imshow(window_name.append(to_string(idx)), gpu_frame.at(idx));
// 		}
// 	#else
// 		for (int idx = 0; idx < N; idx++) {
// 			imshow(window_name.append(to_string(idx)), multicam.at(idx).preview);
// 		}
// 	#endif
// #endif

// #ifdef ENABLE_DISPLAY
// 		if(waitKey(1) == 27) break;
// #endif
	}

	for (int idx = 0; idx < N; idx++) {
		multicam.at(idx).stop_thread();
	}
	
	/*
	 * Helper function to free allocated resources and close the camera device.
	 */
	bool deinit = false;
	for (int idx = 0; idx < N; idx++) {
		deinit = deinit || (multicam.at(idx).helper_deinit_cam() < 0);
	}
	if (deinit) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
