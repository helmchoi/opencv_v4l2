#include <stdio.h>
#include <stdlib.h>
// #include <string.h>
#include <iostream>

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <v4l2_util.hpp>

#define NUM_BUFFS	4
#define CLEAR(x) memset(&(x), 0, sizeof(x))

//=====================================================

unsigned int GetTickCount() {
    struct timeval tv;
    if(gettimeofday(&tv, NULL) != 0)
            return 0;

    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

int CamV4L2::xioctl(int fh, unsigned long request, void *arg) {
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

int CamV4L2::set_io_method(enum io_method io_meth) {
	switch (io_meth)
	{
		case IO_METHOD_READ:
		case IO_METHOD_MMAP:
		case IO_METHOD_USERPTR:
			io = io_meth;
			return 0;
		default:
			fprintf(stderr, "Invalid I/O method\n");
			return ERR;
	}
}

int CamV4L2::stop_capturing(void) {
	enum v4l2_buf_type type;
	struct v4l2_requestbuffers req;

	switch (io) {
		case IO_METHOD_READ:
			/* Nothing to do. */
			break;

		case IO_METHOD_MMAP:
		case IO_METHOD_USERPTR:
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
			{
				fprintf(stderr, "Error occurred when streaming off\n");
				return ERR;
			}
			break;
	}

	CLEAR(req);

	/*
	 * Request to clear the buffers. This helps with changing resolution.
	 */
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "The device does not support "
					"memory mapping\n");
		}
		return ERR;
	}

	return 0;
}

int CamV4L2::start_capturing(void) {
	unsigned int i;
	enum v4l2_buf_type type;

	switch (io) {
		case IO_METHOD_READ:
			/* Nothing to do. */
			break;

		case IO_METHOD_MMAP:
			for (i = 0; i < n_buffers; ++i) {
				struct v4l2_buffer buf;

				CLEAR(buf);
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_MMAP;
				buf.index = i;

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				{
					fprintf(stderr, "Error occurred when queueing buffer\n");
					return ERR;
				}
			}
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			{
				fprintf(stderr, "Error occurred when turning on stream\n");
				return ERR;
			}
			break;

		case IO_METHOD_USERPTR:
			for (i = 0; i < n_buffers; ++i) {
				struct v4l2_buffer buf;

				CLEAR(buf);
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_USERPTR;
				buf.index = i;
				buf.m.userptr = (unsigned long)buffers[i].start;
				buf.length = buffers[i].length;

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				{
					fprintf(stderr, "Error occurred when queueing buffer\n");
					return ERR;
				}
			}
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			{
				fprintf(stderr, "Error when turning on stream\n");
				return ERR;
			}
			break;
	}

	return 0;
}

int CamV4L2::uninit_device(void) {
	unsigned int i;
	int ret = 0;

	switch (io) {
		case IO_METHOD_READ:
			free(buffers[0].start);
			break;

		case IO_METHOD_MMAP:
			for (i = 0; i < n_buffers; ++i)
				if (-1 == munmap(buffers[i].start, buffers[i].length))
					ret = ERR;
			break;

		case IO_METHOD_USERPTR:
			for (i = 0; i < n_buffers; ++i)
				free(buffers[i].start);
			break;
	}

	free(buffers);
	return ret;
}

int CamV4L2::init_read(unsigned int buffer_size) {
	buffers = (struct buffer *) calloc(1, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		return ERR;
	}

	buffers[0].length = buffer_size;
	buffers[0].start = malloc(buffer_size);

	if (!buffers[0].start) {
		fprintf(stderr, "Out of memory\n");
		return ERR;
	}

	return 0;
}

int CamV4L2::init_mmap(void) {
	struct v4l2_requestbuffers req;
	int ret = 0;

	CLEAR(req);

	req.count = NUM_BUFFS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "The device does not support "
					"memory mapping\n");
		}
		return ERR;
	}

	if (req.count < 1) {
		fprintf(stderr, "Insufficient memory to allocate "
				"buffers");
		return ERR;
	}

	buffers = (struct buffer *) calloc(req.count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		return ERR;
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		int loop_err = 0;
		struct v4l2_buffer buf;

		CLEAR(buf);
		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = n_buffers;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
		{
			fprintf(stderr, "Error occurred when querying buffer\n");
			loop_err = 1;
			goto LOOP_FREE_EXIT;
		}

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start =
			mmap(NULL /* start anywhere */,
					buf.length,
					PROT_READ | PROT_WRITE /* required */,
					MAP_SHARED /* recommended */,
					fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start) {
			fprintf(stderr, "Error occurred when mapping memory\n");
			loop_err = 1;
			goto LOOP_FREE_EXIT;
		}

LOOP_FREE_EXIT:
		if (loop_err)
		{
			unsigned int curr_buf_to_free;
			for (curr_buf_to_free = 0;
				curr_buf_to_free < n_buffers;
				curr_buf_to_free++)
			{
				if (
					munmap(buffers[curr_buf_to_free].start,
					buffers[curr_buf_to_free].length) != 0
				)
				{
					/*
					 * Errors ignored as mapping itself
					 * failed for a buffer
					 */
				}
			}
			free(buffers);
			return ERR;
		}
	}

	return ret;
}

int CamV4L2::init_userp(unsigned int buffer_size) {
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count  = NUM_BUFFS;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "The device does not "
					"support user pointer i/o\n");
		}
		return ERR;
	}

	buffers = (struct buffer *) calloc(req.count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		return ERR;
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		if(posix_memalign(&buffers[n_buffers].start,getpagesize(),buffer_size) != 0)
		{
			/*
			 * This happens only in case of ENOMEM
			 */
			unsigned int curr_buf_to_free;
			for (curr_buf_to_free = 0;
				curr_buf_to_free < n_buffers;
				curr_buf_to_free++
			)
			{
				free(buffers[curr_buf_to_free].start);
			}
			free(buffers);
			fprintf(stderr, "Error occurred when allocating memory for buffers\n");
			return ERR;
		}
	}

	return 0;
}

int CamV4L2::init_device(unsigned int width, 
    unsigned int height, unsigned int format) {
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	unsigned int min;

	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "Given device is no V4L2 device\n");
		}
		fprintf(stderr, "Error occurred when querying capabilities\n");
		return ERR;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "Given device is no video capture device\n");
		return ERR;
	}

	switch (io) {
		case IO_METHOD_READ:
			if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
				fprintf(stderr, "Given device does not "
						"support read i/o\n");
				return ERR;
			}
			break;

		case IO_METHOD_MMAP:
		case IO_METHOD_USERPTR:
			if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
				fprintf(stderr, "Given device does not "
						"support streaming i/o\n");
				return ERR;
			}
			break;
	}


	/* Select video input, video standard and tune here. */

	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; /* reset to default */

		if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
			switch (errno) {
				case EINVAL:
					/* Cropping not supported. */
					break;
				default:
					/* Errors ignored. */
					break;
			}
		}
	} else {
		/* Errors ignored. */
	}

	CLEAR(fmt);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = width;
	fmt.fmt.pix.height      = height;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
	{
		fprintf(stderr, "Error occurred when trying to set format\n");
		return ERR;
	}

	/* Note VIDIOC_S_FMT may change width and height. */

	printf("pixfmt = %c %c %c %c \n", (fmt.fmt.pix.pixelformat & 0x000000ff) , (fmt.fmt.pix.pixelformat & 0x0000ff00) >>8 , (fmt.fmt.pix.pixelformat & 0x00ff0000) >>16, (fmt.fmt.pix.pixelformat & 0xff000000) >>24 );
	printf("width = %d height = %d\n",fmt.fmt.pix.width,fmt.fmt.pix.height);

	if (
		fmt.fmt.pix.width != width ||
		fmt.fmt.pix.height != height ||
		fmt.fmt.pix.pixelformat != format
	)
	{
		fprintf(stderr, "Warning: The current format does not match requested format/resolution!\n");
		return ERR;
	}

	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	switch (io) {
		case IO_METHOD_READ:
			return init_read(fmt.fmt.pix.sizeimage);
			break;

		case IO_METHOD_MMAP:
			return init_mmap();
			break;

		case IO_METHOD_USERPTR:
			return init_userp(fmt.fmt.pix.sizeimage);
			break;
	}

	return 0;
}

int CamV4L2::close_device(void) {
	if (-1 == close(fd))
	{
		fprintf(stderr, "Error occurred when closing device\n");
		return ERR;
	}

	fd = -1;

	return 0;
}

int CamV4L2::helper_init_cam(int idx, const char* devname, 
    unsigned int width, unsigned int height, 
    unsigned int format, enum io_method io_meth, bool enable_display_) {
	
	camidx = idx;
	enable_display = enable_display_;
	running = false;
    if (is_initialised)
	{
		/*
		 * The library currently doesn't support
		 * accessing multiple devices simultaneously.
		 * So, return an error when such access is attempted.
		 */
		fprintf(stderr, "Cannot use the library to initialise multiple devices, simultaneously.\n");
		return ERR;
	}

	std::cout << "--- Camera #" << camidx << "(" << devname << ") ---------------" << std::endl;
	if(
		set_io_method(io_meth) < 0 ||
		open_device(devname) < 0 ||
		init_device(width,height,format) < 0 ||
		start_capturing() < 0
	)
	{
		fprintf(stderr, "Error occurred when initialising camera\n");
		return ERR;
	}

	yuyv_frame = cv::Mat(height, width, CV_8UC2);

	if (enable_display) {
		std::string savepath_ = "../log/";
		savepath = savepath_.append(std::to_string(camidx)).append("/");
	// 	std::string winname_ = "camera #";
	// 	winname = winname_.append(std::to_string(camidx));
	// 	cv::namedWindow(winname);
	}

	is_initialised = 1;
	return 0;
}

int CamV4L2::helper_get_cam_frame(
    unsigned char** pointer_to_cam_data, int *size) {
    static unsigned char max_timeout_retries = 10;
	unsigned char timeout_retries = 0;

	if (!is_initialised)
	{
		fprintf (stderr, "Error: trying to get frame without successfully initialising camera\n");
		return ERR;
	}

	if (!is_released)
	{
		fprintf (stderr, "Error: trying to get another frame without releasing already obtained frame\n");
		return ERR;
	}

	for (;;) {
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(fd + 1, &fds, NULL, NULL, &tv);

		if (-1 == r) {
			if (EINTR == errno)
				continue;
		}

		if (0 == r) {
			fprintf(stderr, "select timeout\n");
			timeout_retries++;

			if (timeout_retries == max_timeout_retries)
			{
				fprintf(stderr, "Could not get frame after multiple retries\n");
				return ERR;
			}
		}

		CLEAR(frame_buf);
		frame_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (-1 == xioctl(fd, VIDIOC_DQBUF, &frame_buf)) {
			switch (errno) {
				case EAGAIN:
					continue;

				case EIO:
					/* Could ignore EIO, see spec. */

					/* fall through */

				default:
					continue;
			}
		}
		*pointer_to_cam_data = (unsigned char*) buffers[frame_buf.index].start;
		*size = frame_buf.bytesused;
		break;
		/* EAGAIN - continue select loop. */
	}

	is_released = 0;
	return 0;
}

void CamV4L2::start_thread() {
	runner = std::thread(&CamV4L2::run_thread, this);
}

void CamV4L2::stop_thread() {
	running = false;
	runner.join();
}

int CamV4L2::run_thread() {
	std::cout << "start thread #" << camidx << std::endl;
	start = GetTickCount();
	while (running) {
		if (helper_get_cam_frame(&ptr_cam_frame, &bytes_used) < 0) {
			return -1;
			break;
		}

		/*
			* It's easy to re-use the matrix for our case (V4L2 user pointer) by changing the
			* member 'data' to point to the data obtained from the V4L2 helper.
			*/
		yuyv_frame.data = ptr_cam_frame;
		if (yuyv_frame.empty()) {
			std::cout << "cam #" << camidx << ": Img load failed" << std::endl;
			return -1;
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
		cv::cvtColor(yuyv_frame, preview, cv::COLOR_YUV2BGR_UYVY);
		if (enable_display) {
			// cv::imshow(winname, preview);
			// cv::waitKey(1);
			cv::imwrite(savepath + std::to_string(savecnt) + ".png", preview);
			savecnt++;
		}
		
		if (helper_release_cam_frame() < 0) {
			break;
		}

		fps++;
		end = GetTickCount();
		if ((end - start) >= 1000) {
			std::cout << "cam #" << camidx << " - fps = " << fps << std::endl ;
			fps = 0;
			start = end;
		}
	}
	return 0;
}

int CamV4L2::helper_release_cam_frame() {
    if (!is_initialised)
	{
		fprintf (stderr, "Error: trying to release frame without successfully initialising camera\n");
		return ERR;
	}

	if (is_released)
	{
		fprintf (stderr, "Error: trying to release already released frame\n");
		return ERR;
	}

	if (-1 == xioctl(fd, VIDIOC_QBUF, &frame_buf))
	{
		fprintf(stderr, "Error occurred when queueing frame for re-capture\n");
		return ERR;
	}

	/*
	 * We assume the frame hasn't been released if an error occurred as
	 * we couldn't queue the frame for streaming.
	 *
	 * Assuming it to be released in case an error occurs causes issues
	 * such as the loss of a buffer, etc.
	 */
	is_released = 1;
	return 0;
}

int CamV4L2::helper_deinit_cam() {
    if (!is_initialised)
	{
		fprintf(stderr, "Error: trying to de-initialise without initialising camera\n");
		return ERR;
	}

	/*
	 * It's better to turn off is_initialised even if the
	 * de-initialisation fails as it shouldn't have affect
	 * re-initialisation a lot.
	 */
	is_initialised = 0;

	if(
		stop_capturing() < 0 ||
		uninit_device() < 0 ||
		close_device() < 0
	)
	{
		fprintf(stderr, "Error occurred when de-initialising camera\n");
		return ERR;
	}

	return 0;
}

int CamV4L2::open_device(const char *dev_name)
{
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n",
				dev_name, errno, strerror(errno));
		return ERR;
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		return ERR;
	}

	fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n",
				dev_name, errno, strerror(errno));
		return ERR;
	}

	return fd;
}