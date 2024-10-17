#include "common.h"
#include "tengine/c_api.h"
#include "tengine_operations.h"
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <fstream>
#include <cstdint>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <thread>
#include <atomic>
#include <ctime>

#define DEFAULT_MAX_BOX_COUNT 100
#define DEFAULT_REPEAT_COUNT 1
#define DEFAULT_THREAD_COUNT 1

#define LETTERBOX_ROWS 256
#define LETTERBOX_COLS 256
#define MODEL_CHANNELS 3

#define INFRARED_GPIO 65

#define SYSFS_GPIO_DIR "/sys/class/gpio"

struct framebuffer_info
{
    uint32_t bits_per_pixel;
    uint32_t xres_virtual;
};

std::atomic<bool> infreared_thread_active(false);
std::atomic<bool> message_ui(false);
std::mutex mtx;

static int gpio_export(unsigned int gpio);
static int gpio_unexport(unsigned int gpio);
static int gpio_set_dir(unsigned int gpio, unsigned int out_flag);
static int gpio_set_value(unsigned int gpio, unsigned int value);
static int gpio_get_value(unsigned int gpio, int *value);
static int gpio_fd_open(unsigned int gpio);
static int gpio_fd_close(int fd);

int get_input_fp32_data_square(const cv::Mat &img, float *input_data, float *mean, float *scale);
struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path);
void post_process_ssd(cv::Mat &frame, float threshold, const float *outdata, int num);

void show_usage()
{
    fprintf(stderr, "[Usage]:  [-h]\n    [-m model_file] [-r repeat_count] [-t thread_count]\n");
}

void sensor_operation(cv::Mat &shared_frame, float *input_data, graph_t &graph)
{

    float mean[3] = {127.5f, 127.5f, 127.5f};
    float scale[3] = {0.007843f, 0.007843f, 0.007843f};
    float show_threshold = 0.5f;
    int res = 0;

    res = get_input_fp32_data_square(shared_frame, input_data, mean, scale);
    if (res < 0)
    {
        infreared_thread_active = false;
        return;
    }
    if (run_graph(graph, 1) < 0)
    {
        infreared_thread_active = false;
        fprintf(stderr, "Run graph failed\n");
        return;
    }

    tensor_t output_tensor = get_graph_output_tensor(graph, 0, 0);
    int out_dim[4];
    get_tensor_shape(output_tensor, out_dim, 4);
    float *output_data = (float *)get_tensor_buffer(output_tensor);
    post_process_ssd(shared_frame, show_threshold, output_data, out_dim[1]);

    infreared_thread_active = false;
}

void capture_camera_data(std::atomic<bool> &run_flag, cv::Mat &shared_frame)
{

    const int frame_width = 640;
    const int frame_height = 480;
    const int frame_rate = 10;
    double start = 0, end = 0;
    std::ofstream ofs("/dev/fb0");
    cv::Mat frame;
    cv::Mat rotation_fream;
    cv::Mat image;

    time_t *timep = (time_t *)malloc(sizeof(*timep));
    struct tm *p;

    framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");

    int framebuffer_width = fb_info.xres_virtual;
    int framebuffer_depth = fb_info.bits_per_pixel;

    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        fprintf(stderr, "Error opening video stream\n");
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, frame_width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, frame_height);
    cap.set(cv::CAP_PROP_FPS, frame_rate);

    while (run_flag)
    {
        time(timep);
        p = localtime(timep);

        if (message_ui)
        {

            mtx.lock();
            message_ui = false;
            mtx.unlock();
            start = get_current_time();
            int hour0, min0, hour1, min1;
            char data0[256] = {};
            char read_data[512];
            char time_now_data[256];

            int fd = open("/www/cgi-bin/web.log", O_RDONLY);
            if (fd < 0)
            {
                fprintf(stderr, "error\n:%d\n", fd);
            }
            read(fd, read_data, sizeof(read_data));
            close(fd);

            sscanf(read_data, "%d:%d:%d:%d:%s", &hour0, &min0, &hour1, &min1, data0);

            if ((p->tm_hour > hour0 && p->tm_hour < hour1) || (p->tm_hour == hour0 && p->tm_min > min0) || (p->tm_hour == hour1 && p->tm_min < min1))
            {
                frame = cv::Mat(frame_height, frame_width, CV_8UC3, cv::Scalar(255, 255, 255)); // 创建一个640x480的白色背景图像

                int font_face = cv::FONT_HERSHEY_SIMPLEX;
                double font_scale = 1.0;
                int thickness = 2;
                cv::Point text_org(50, frame_height / 2);

                cv::putText(frame, data0, text_org, font_face, font_scale, cv::Scalar(0, 0, 0), thickness);
            }
            else
            {
                start = 0;
                cap >> frame;
                if (frame.empty())
                {
                    fprintf(stderr, "Received empty frame\n");
                    break;
                }
            }
        }
        else
        {
            end = get_current_time();
            if (end - start > 10000)
            {
                cap >> frame;
                if (frame.empty())
                {
                    fprintf(stderr, "Received empty frame\n");
                    break;
                }
            }
        }

        cv::Size2f frame_size = frame.size();
        cv::Mat framebuffer_compat;

        switch (framebuffer_depth)
        {
        case 16:
            cv::cvtColor(frame, framebuffer_compat, cv::COLOR_BGR2BGR565);
            for (int y = 0; y < frame_size.height; y++)
            {
                ofs.seekp(y * framebuffer_width * 2);
                ofs.write(reinterpret_cast<char *>(framebuffer_compat.ptr(y)), frame_size.width * 2);
            }
            break;
        case 32:
        {
            std::vector<cv::Mat> split_bgr;
            cv::split(frame, split_bgr);
            split_bgr.push_back(cv::Mat(frame_size, CV_8UC1, cv::Scalar(255)));
            cv::merge(split_bgr, framebuffer_compat);
            for (int y = 0; y < frame_size.height; y++)
            {
                ofs.seekp(y * framebuffer_width * 4);
                ofs.write(reinterpret_cast<char *>(framebuffer_compat.ptr(y)), frame_size.width * 4);
            }
        }
        break;
        default:
            fprintf(stderr, "Unsupported depth of framebuffer\n");
        }

        shared_frame = frame.clone(); // 将捕获的帧拷贝到共享帧中
    }

    cap.release();
    cv::destroyAllWindows();
}
int main(int argc, char *argv[])
{
    int value;

    int repeat_count = DEFAULT_REPEAT_COUNT;
    int num_thread = DEFAULT_THREAD_COUNT;
    char *model_file = NULL;
    int img_h = 300;
    int img_w = 300;

    int res;
    while ((res = getopt(argc, argv, "m:r:t:h:")) != -1)
    {
        switch (res)
        {
        case 'm':
            model_file = optarg;
            break;
        case 'r':
            repeat_count = atoi(optarg);
            break;
        case 't':
            num_thread = atoi(optarg);
            break;
        case 'h':
            show_usage();
            return 0;
        default:
            break;
        }
    }

    if (model_file == NULL)
    {
        fprintf(stderr, "Error: Tengine model file not specified!\n");
        show_usage();
        return -1;
    }

    // 红外初始化
    gpio_export(INFRARED_GPIO);
    gpio_set_dir(INFRARED_GPIO, 0);

    // tengine init
    struct options opt;
    opt.num_thread = num_thread;
    opt.cluster = TENGINE_CLUSTER_ALL;
    opt.precision = TENGINE_MODE_FP32;
    opt.affinity = 0;

    init_tengine();

    graph_t graph = create_graph(NULL, "tengine", model_file);
    if (graph == NULL)
    {
        fprintf(stderr, "Create graph failed.\n");
        return -1;
    }

    int img_size = img_h * img_w * 3;
    int dims[] = {1, 3, img_h, img_w};
    float *input_data = (float *)malloc(img_size * sizeof(float));

    tensor_t input_tensor = get_graph_input_tensor(graph, 0, 0);
    if (input_tensor == NULL)
    {
        fprintf(stderr, "Get input tensor failed\n");
        return -1;
    }

    if (set_tensor_shape(input_tensor, dims, 4) < 0 || set_tensor_buffer(input_tensor, input_data, img_size * sizeof(float)) < 0 || prerun_graph_multithread(graph, opt) < 0)
    {
        fprintf(stderr, "failed\n");
        return -1;
    }

    std::atomic<bool> run_flag(true);                                                           // 用于控制主线程的运行状态
    cv::Mat shared_frame;                                                                       // 共享的摄像头帧
    std::thread camera_thread(capture_camera_data, std::ref(run_flag), std::ref(shared_frame)); // 主线程处理摄像头
    sleep(3);
    std::thread sensor_thread;

    while (run_flag)
    {
        res = gpio_get_value(INFRARED_GPIO, &value);
        if (res < 0)
        {
            fprintf(stderr, "INFRARED error\n");
        }

        if (value == 1 && !infreared_thread_active)
        {
            fprintf(stderr, "识别中\n");

            if (sensor_thread.joinable())
            {
                sensor_thread.join(); // 等待之前的子线程完成
            }
            infreared_thread_active = true;
            sensor_thread = std::thread(sensor_operation, std::ref(shared_frame), input_data, std::ref(graph));
        }
    }

    run_flag = false; // 停止摄像头数据捕获
    if (camera_thread.joinable())
        camera_thread.join();

    if (sensor_thread.joinable())
        sensor_thread.join(); // 等待最后的子线程完成

    free(input_data);
    postrun_graph(graph);
    destroy_graph(graph);
    release_tengine();

    return 0;
}

static int gpio_export(unsigned int gpio)
{
    int fd, len;
    char buf[32];

    fd = open(SYSFS_GPIO_DIR "/export", O_WRONLY);
    if (fd < 0)
    {
        fprintf(stderr, "gpio/export open error\n");
        return fd;
    }

    len = snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, len);
    close(fd);

    return 0;
}
static int gpio_unexport(unsigned int gpio)
{
    int fd, len;
    char buf[64];

    fd = open(SYSFS_GPIO_DIR "/unexport", O_WRONLY);
    if (fd < 0)
    {
        fprintf(stderr, "gpio/unexport open error\n");
        return fd;
    }

    len = snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, len);
    close(fd);
    return 0;
}
static int gpio_set_dir(unsigned int gpio, unsigned int out_flag)
{
    int fd, len;
    char buf[64];

    len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/direction", gpio);

    fd = open(buf, O_WRONLY);
    if (fd < 0)
    {
        fprintf(stderr, "gpio set dir error\n");
        return fd;
    }

    if (out_flag)
        write(fd, "out", 3);
    else
        write(fd, "in", 2);

    close(fd);
    return 0;
}
static int gpio_set_value(unsigned int gpio, unsigned int value)
{
    int fd, len;
    char buf[64];

    len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

    fd = open(buf, O_WRONLY);
    if (fd < 0)
    {
        fprintf(stderr, "gpio set value error\n");
        return fd;
    }

    if (value)
        write(fd, "1", 1);
    else
        write(fd, "0", 1);

    close(fd);
    return 0;
}

static int gpio_get_value(unsigned int gpio, int *value)
{
    int fd, len;
    char buf[64];
    char ch;

    len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

    fd = open(buf, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "gpio get value error\n");
        return fd;
    }

    read(fd, &ch, 1);

    if (ch != '0')
    {
        *value = 1;
    }
    else
    {
        *value = 0;
    }

    close(fd);
    return 0;
}

static int gpio_fd_open(unsigned int gpio)
{
    int fd, len;
    char buf[64];

    len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

    fd = open(buf, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        fprintf(stderr, "gpio open error\n");
    }
    return fd;
}

static int gpio_fd_close(int fd)
{
    return close(fd);
}

int get_input_fp32_data_square(const cv::Mat &img, float *input_data, float *mean, float *scale)
{
    if (img.empty())
    {
        std::cerr << "Input image is empty!" << std::endl;
        return -1;
    }

    float scale_letterbox;
    int resize_rows;
    int resize_cols;

    if ((LETTERBOX_ROWS * 1.0 / img.rows) < (LETTERBOX_COLS * 1.0 / img.cols * 1.0))
        scale_letterbox = 1.0 * LETTERBOX_ROWS / img.rows;
    else
        scale_letterbox = 1.0 * LETTERBOX_COLS / img.cols;

    resize_cols = static_cast<int>(scale_letterbox * img.cols);
    resize_rows = static_cast<int>(scale_letterbox * img.rows);

    cv::Mat resized_img;
    cv::resize(img, resized_img, cv::Size(resize_cols, resize_rows));
    resized_img.convertTo(resized_img, CV_32FC3);

    cv::Mat img_new(LETTERBOX_ROWS, LETTERBOX_COLS, CV_32FC3,
                    cv::Scalar(0.5 / scale[0] + mean[0],
                               0.5 / scale[1] + mean[1],
                               0.5 / scale[2] + mean[2]));

    int top = (LETTERBOX_ROWS - resize_rows) / 2;
    int bot = (LETTERBOX_ROWS - resize_rows + 1) / 2;
    int left = (LETTERBOX_COLS - resize_cols) / 2;
    int right = (LETTERBOX_COLS - resize_cols + 1) / 2;

    cv::copyMakeBorder(resized_img, img_new, top, bot, left, right, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    float *img_data = (float *)img_new.data;

    for (int h = 0; h < LETTERBOX_ROWS; h++)
    {
        for (int w = 0; w < LETTERBOX_COLS; w++)
        {
            for (int c = 0; c < MODEL_CHANNELS; c++)
            {
                int in_index = h * LETTERBOX_COLS * MODEL_CHANNELS + w * MODEL_CHANNELS + c;
                int out_index = c * LETTERBOX_ROWS * LETTERBOX_COLS + h * LETTERBOX_COLS + w;
                input_data[out_index] = (img_data[in_index] - mean[c]) * scale[c];
            }
        }
    }
    return 0;
}
struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path)
{
    struct framebuffer_info info;
    struct fb_var_screeninfo screen_info;
    int fd = -1;
    fd = open(framebuffer_device_path, O_RDWR);
    if (fd >= 0)
    {
        if (!ioctl(fd, FBIOGET_VSCREENINFO, &screen_info))
        {
            info.xres_virtual = screen_info.xres_virtual;
            info.bits_per_pixel = screen_info.bits_per_pixel;
        }
    }
    return info;
}

void post_process_ssd(cv::Mat &frame, float threshold, const float *outdata, int num)
{
    const char *class_names[] = {
        "background", "aeroplane", "bicycle", "bird", "boat", "bottle",
        "bus", "car", "cat", "chair", "cow", "diningtable",
        "dog", "horse", "motorbike", "person", "pottedplant", "sheep",
        "sofa", "train", "tvmonitor"};

    mtx.lock();
    for (int i = 0; i < num; i++)
    {

        if ((int)outdata[0] == 15 && outdata[1] * 10 > 80)
        // if (1)
        {
            message_ui = true;
            break;
        }
        else
            message_ui = false;
        // fprintf(stderr, "%s\t:%.1f%%\n", class_names[(int)outdata[0]], outdata[1] * 100);
        outdata += 6;
    }
    mtx.unlock();
}