/*
 * opencv_v4l2 - v4l2_helper.h file
 *
 * Copyright (c) 2017-2018, e-con Systems India Pvt. Ltd.  All rights reserved.
 *
 */
// Header file for v4l2_helper functions.


#define GET 1
#define SET 2
#include <linux/videodev2.h>

#define ERR -128

enum io_method {
    IO_METHOD_READ = 1,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR
};

class CamV4L2{
    private:
        int open_device(const char *dev_name);
        int xioctl(int fh, unsigned long request, void *arg);
        int set_io_method(enum io_method io_meth);
        int stop_capturing(void);
        int start_capturing(void);
        int uninit_device(void);
        int init_read(unsigned int buffer_size);
        int init_mmap(void);
        int init_userp(unsigned int buffer_size);
        int init_device(unsigned int width, unsigned int height, 
                        unsigned int format);
        int close_device(void);

    public:
        enum io_method io = IO_METHOD_MMAP;
        int fd = -1;
        struct buffer *buffers;
        unsigned int n_buffers;
        struct v4l2_buffer frame_buf;
        char is_initialised = 0;
        char is_released = 1;
        
        int helper_init_cam(const char* devname, 
                            unsigned int width, unsigned int height, 
                            unsigned int format, enum io_method io_meth);
        int helper_get_cam_frame(unsigned char** pointer_to_cam_data, int *size);
        int helper_release_cam_frame();
        int helper_deinit_cam();

        //int helper_change_cam_res(unsigned int width, unsigned int height, unsigned int format, enum io_method io_meth);
        //int helper_ctrl(unsigned int, int,int*);
        //int helper_queryctrl(unsigned int,struct v4l2_queryctrl* );
}