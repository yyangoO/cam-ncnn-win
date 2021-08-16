#ifndef MAIN_ACTIVITY_JNI_H
#define MAIN_ACTIVITY_JNI_H


#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImageReader.h>
#include <android/bitmap.h>

#include <functional>
#include <thread>
#include <string>
#include <vector>
#include <map>

#include "layer.h"
#include "net.h"
#include "benchmark.h"

#include "jni.h"


/**
 * @brief previte indices
 * the previte indices
 */
enum PREVIEW_INDICES
{
    PREVIEW_REQUEST_IDX = 0,    ///< preview request index
    CAPTURE_REQUEST_COUNT,      ///< capture request count
};


/**
 * @brief app source data type
 * app source data type
 */
enum APP_SOURCE_TYPE
{
    APP_SOURCE_NONE = 0,    ///< app no source
    APP_SOURCE_CAMERA,      ///< app camera source
    APP_SOURCE_PICTURE      ///< app picture source
};


/**
 * @brief camera capture session
 * initialize the NDK Camera2 CaptureSession state
 */
enum class CaptureSessionState : int32_t
{
    READY = 0,  ///< session is ready
    ACTIVE,     ///< session is busy
    CLOSED,     ///< session is closed (by itself or a new session evicts)
    MAX_STATE
};


/**
 * @brief capture request information
 * the capture request information
 */
struct CaptureRequestInfo
{
    ANativeWindow* output_native_window_;       ///< output native window of request
    ACaptureSessionOutput* session_output_;     ///< output capture session
    ACameraOutputTarget* target_;               ///< camera output target
    ACaptureRequest* request_;                  ///< capture request
    ACameraDevice_request_template template_;   ///< camera device request template
    int session_sequence_id_;                   ///< camera session sequence ID
};


/**
 * @brief data structure of image format
 * a data structure to communicate resolution between camera and image reader
 */
struct ImageFormat
{
    int32_t width;      ///< width
    int32_t height;     ///< height

    int32_t format;     ///< in this demo, the format is fixed to YUV_420
};


struct NcnnRet
{
    AHardwareBuffer* hb;        ///< the android hardwarebuffer
    std::string ret;            ///< the result
};


/**
 * @brief get the absolute value from relative value
 * this template return the absolute value from relative value
 */
template <typename T> class RangeValue
{
public:
    T min_;     ///< miminum value
    T max_;     ///< maximum value

    /**
     * @brief get the value's percent in range
     * this template return the value's percent in range
     * @param percent       input percent (50 for 50%)
     * @return              absolute value from relative value
     */
    T value(int percent)
    {
        return static_cast<T>(min_ + (max_ - min_) * percent / 100);
    }

    /**
     * @brief construct function
     * this function initialize the maximum and minimum value
     */
    RangeValue()
    {
        this->min_ = static_cast<T>(0);
        this->max_ = static_cast<T>(0);
    }

    /**
     * @brief know if it's support or not
     * know if it's support or not
     */
    bool Supported(void) const
    {
        return (min_ != max_);
    }
};


/**
 * @brief helper classes to hold enumerated camera
 * helper classes to hold enumerated camera
 */
class CameraId
{
public:
    ACameraDevice* device_;                                 ///< android camera device

    std::string id_;                                        ///< camera ID
    acamera_metadata_enum_android_lens_facing_t facing_;    ///< android camera's facing direction
    bool available_flag_;                                   ///< free to use (no other apps are using)
    bool own_flag_;                                         ///< we are the owner of the camera or not

public:
    /**
     * @brief construct function
     * initialize the attributes of this class
     */
    explicit CameraId(const char* id)
            : device_(nullptr),
              facing_(ACAMERA_LENS_FACING_FRONT),
              available_flag_(false),
              own_flag_(false)
    {
        id_ = id;
    }
    explicit CameraId(void)
    {
        CameraId("");
    }
};


/**
 * @brief a helper class to assist image size comparison, by comparing the absolute size
 * assist image size comparison, by comparing the absolute size, regardless of the portrait or landscape mode.
 */
class DisplayDimension
{
private:
    int32_t _w;             ///< width
    int32_t _h;             ///< height
    bool _portrait_flag;    ///< portrait
public:
    /**
     * @brief construct function
     * initialize the attributes of this class
     * @param w     the width
     * @param h     the height
     */
    DisplayDimension(int32_t w, int32_t h) : _w(w), _h(h), _portrait_flag(false)
    {
        if (h > w) {
            // make it landscape
            this->_w = h;
            this->_h = w;
            this->_portrait_flag = true;
        }
    }
    /**
     * @brief construct function
     * initialize the attributes of this class
     * @param other     other {@link DisplayDimension} object adress
     */
    DisplayDimension(const DisplayDimension& other)
    {
        this->_w = other._w;
        this->_h = other._h;
        this->_portrait_flag = other._portrait_flag;
    }
    /**
     * @brief construct function
     * initialize the attributes of this class
     */
    DisplayDimension(void)
    {
        this->_w = 0;
        this->_h = 0;
        this->_portrait_flag = false;
    }
    /**
     * @brief reconstruct operator "="
     * reconstruct operator "="
     * @param other     other {@link DisplayDimension} object adress
     */
    DisplayDimension& operator=(const DisplayDimension& other)
    {
        this->_w = other._w;
        this->_h = other._h;
        this->_portrait_flag = other._portrait_flag;
        return (*this);
    }
    /**
     * @brief check if have same ratio or not
     * check if have same ratio or not
     * @param other     other {@link DisplayDimension} object adress
     * @return          true if same, false if not same
     */
    bool IsSameRatio(DisplayDimension& other)
    {
        return (this->_w * other._h == this->_h * other._w);
    }
    /**
     * @brief check if it is bigger than other
     * check if it is bigger than other
     * @param other     other {@link DisplayDimension} object adress
     * @return          true if same, false if not same
     */
    bool operator>(DisplayDimension& other)
    {
        return (this->_w >= other._w && this->_h >= other._h);
    }
    /**
     * @brief check if it is equal with other
     * check if it is equal with other
     * @param other     other {@link DisplayDimension} object adress
     * @return          true if same, false if not same
     */
    bool operator==(DisplayDimension& other)
    {
        return (this->_w == other._w && this->_h == other._h && this->_portrait_flag == other._portrait_flag);
    }
    /**
     * @brief get the short of two value
     * get the short of two value
     * @param other     other {@link DisplayDimension} object adress
     * @return          the result
     */
    DisplayDimension operator-(DisplayDimension& other)
    {
        DisplayDimension delta(this->_w - other._w, this->_h - other._h);
        return delta;
    }
    /**
     * @brief flip the portrait flag
     * flip the portrait flag
     */
    void flip(void)
    {
        this->_portrait_flag = !this->_portrait_flag;
    }
    /**
     * @brief check the portrait flag
     * check the portrait flag
     * @return      the portrait flag
     */
    bool IsPortrait(void)
    {
        return this->_portrait_flag;
    }
    /**
     * @brief get the width
     * get the width
     * @return      the width
     */
    int32_t width(void)
    {
        return this->_w;
    }
    /**
     * @brief get the height
     * get the height
     * @return      the height
     */
    int32_t height(void)
    {
        return this->_h;
    }
    /**
     * @brief origin by the width
     * origin by the width
     * @return      the width
     */
    int32_t org_width(void)
    {
        return (this->_portrait_flag ? this->_h : this->_w);
    }
    /**
     * @brief origin by the height
     * origin by the height
     * @return      the height
     */
    int32_t org_height(void)
    {
        return (this->_portrait_flag ? this->_w : this->_h);
    }
};


/**
 * @brief native camera
 * the native camera class
 */
class NDKCamera
{
private:
    ACameraManager* _cam_mgr;                                           ///< Android Camera Manager pointer
    std::map<std::string, CameraId> _cams;                              ///< this cameras ID
    std::string _active_cam_id;                                         ///< active camera ID
    uint32_t _cam_facing;                                               ///< camera's facing
    uint32_t _cam_orientation;                                          ///< camera's orientation

    std::vector<CaptureRequestInfo> _requests;                          ///< android camera capture request information

    ACaptureSessionOutputContainer* _output_container;                  ///< need to receive image stream
    ACameraCaptureSession* _capture_session;                            ///< capture session
    CaptureSessionState _capture_session_state;                         ///< capture session's state

    int64_t _exposure_time;                                             ///< camera exposure time
    RangeValue<int64_t> _exposure_range;                                ///< camera exposure range
    int32_t _sensitivity;                                               ///< camera sensitivity
    RangeValue<int32_t> _sensitivity_range;                             ///< camera sensivity range
    volatile bool _valid_flag;                                          ///< true if camera valid

    AImageReader* _reader;                                              ///< the android image reader
    std::function<void(void *ctx, const char* fileName)> _callback;     ///< the callback function
    void* _callback_ctx;                                                ///< the callback function pointer

    ANativeWindow* _native_window;                                      ///< native window
    ImageFormat _img_res;                                               ///< iamge format

private:
    /**
     * @brief enumerate camera
     * enumerate camera we have, find the camera facing back
     */
    void enumerate_cam(void);

public:
    /**
     * @brief construction
     * construction function
     */
    NDKCamera(void);
    /**
     * @brief destruction
     * destruction function
     */
    ~NDKCamera(void);
    /**
     * @brief match the capture size we request
     * match the capture size we request
     * @param display       the {@link ANativeWindow} pointer we want to display
     * @param view          the {@link ImageFormat} we want to view
     * @param capture       the {@link ImageFormat} we capture
     * @return              true on success, false on failure
     */
    bool match_capture_size_request(ANativeWindow* display, ImageFormat* view, ImageFormat* capture);
    /**
     * @brief create a capture session
     * create a capture session of camera
     */
    void create_session(void);
    /**
     * @brief get sensor orientatioin
     * get current sensor's orientation
     * @param facing        the facing of camera
     * @param angle         the angle of camera
     * @return              true on success, false on failure
     */
    bool get_sensor_orientation(int32_t* facing, int32_t* angle);
    /**
     * @brief handle the comaera status changes
     * handle the comaera status changes
     * @param id        camera ID
     * @param available the aviable flag of camera
     */
    void on_cam_status_changed(const char* id, bool available);
    /**
     * @brief handle the camera state
     * handle the camera state
     * @param dev   camera device
     */
    void on_dev_state(ACameraDevice* dev);
    /**
     * @brief handle the camera error
     * handle handle the camera error
     * @param dev   camera device
     * @param err   the error
     */
    void on_dev_error(ACameraDevice* dev, int err);
    /**
     * @brief handle the camera capture session state
     * handle the camera capture session state
     * @param ses       camera captire session
     * @param state     camera captire session state
     */
    void on_session_state(ACameraCaptureSession* ses, CaptureSessionState state);
    /**
     * @brief start request image
     * start request image
     * @param start       start flag
     */
    void start_request(bool start);
    /**
     * @brief init the camera image
     * init the camera image
     * @param res       image resolution
     */
    void init_img(ImageFormat res);
    /**
     * @brief get image
     * get image
     * @return          hadrwarebuffer
     */
    AHardwareBuffer* get_img_hb(void);
    /**
     * @brief get the image's resolution
     * get image's resolution
     * @return      the image format
     */
    ImageFormat get_img_res(void);
    /**
     * @brief get camera manager listener
     * get camera manager listener
     * @return      the callback
     */
    ACameraManager_AvailabilityCallbacks* get_mgr_listener();
    /**
     * @brief get camera device listener
     * get camera device listener
     * @return      the callback
     */
    ACameraDevice_stateCallbacks* get_dev_listener();
    /**
     * @brief get camera session listener
     * get camera session listener
     * @return      the callback
     */
    ACameraCaptureSession_stateCallbacks* get_session_listener();
    /**
     * @brief image reader's callback
     * image reader's callback
     * @param reader     the image reader
     */
    void image_callback(AImageReader* reader);
};


///**
// * @brief camera yuv format image reader
// * camera yuv format image reader
// */
//class YUVImgReader
//{
//private:
//    AImageReader* _reader;                                              ///< the android image reader
//    std::function<void(void *ctx, const char* fileName)> _callback;     ///< the callback function
//    void* _callback_ctx;                                                ///< the callback function pointer
//    AHardwareBuffer_Desc* _hb_desc;                                     ///< android hardwarebuffer descriptor
//
//public:
//    /**
//     * @brief construction
//     * construction function
//     * @param res       the resolution
//     * @param format    the format
//     */
//    YUVImgReader(ImageFormat* res, enum AIMAGE_FORMATS format);
//    /**
//     * @brief destruction
//     * destruction function
//     */
//    ~YUVImgReader(void);
//    /**
//     * @brief get image
//     * get image
//     * @return          hadrwarebuffer
//     */
//    AHardwareBuffer* get_img_hb(void);
//    /**
//     * @brief get the image's resolution
//     * get image's resolution
//     * @return      the image format
//     */
//    ImageFormat get_img_res(void);
//    /**
//     * @brief get the native window
//     * get the native window enstore the image we want
//     * @return      the native window pointer
//     */
//    ANativeWindow* get_native_window(void);
//    /**
//     * @brief delete image
//     * delete image
//     * @param img       the image pointer we want to delete
//     */
//    void delete_img(AImage* img);
//    /**
//     * @brief image reader's callback
//     * image reader's callback
//     * @param reader     the image reader
//     */
//    void image_callback(AImageReader* reader);
//};


/**
 * @brief manage NDK's bitmap
 * manage NDK's bitmap
 */
class NDKPicture
{
private:
    void* _data;                ///< the JNI bitmap
    ImageFormat _img_res;       ///< iamge format

public:
    /**
     * @brief construction
     * construction function
     * @param data      the iamge data
     * @param img_res   image resolution
     */
    NDKPicture(void);
    /**
     * @brief destruction
     * destruction function
     */
    ~NDKPicture(void);
    /**
     * @brief init the bitmap
     * init the bitmap
     * @param data      the iamge data
     * @param img_res   image resolution
     */
    void init_img(void* data, ImageFormat img_res);
    /**
     * @brief get image
     * get image
     * @return          hadrwarebuffer
     */
    AHardwareBuffer* get_img_hb(void);
    /**
     * @brief get the image's resolution
     * get image's resolution
     * @return      the image format
     */
    ImageFormat get_img_res(void);
};


/**
 * @brief manage the native window
 * manage the native window
 */
class NDKWindow
{
private:
//    int32_t _present_rotation;                                          ///< current rotation
//    AImageReader* _reader;                                              ///< the android image reader
//    std::function<void(void *ctx, const char* fileName)> _callback;     ///< the callback function
//    void* _callback_ctx;                                                ///< the callback function pointer

public:
    /**
     * @brief construction
     * construction function
     * @param res       the resolution
     * @param format    the format
     */
    NDKWindow(ImageFormat* res, enum AIMAGE_FORMATS format);
    /**
     * @brief destruction
     * destruction function
     */
    ~NDKWindow();
    /**
     * @brief get the native window
     * get the native window enstore the image we want
     * @return      the native window pointer
     */
    ANativeWindow* get_native_window(void);
    /**
     * @brief get the image's resolution
     * get image's resolution
     * @return      the image format
     */
    ImageFormat get_img_res(void);
    /**
     * @brief get next image
     * get next image
     * @return      the image pointer
     */
    AImage* get_next_img(void);
    /**
     * @brief get last image
     * get last image
     * @return      the image pointer
     */
    AImage* get_latest_img(void);
    /**
     * @brief delete image
     * delete image
     * @param img       the image pointer we want to delete
     */
    void delete_img(AImage* img);
    /**
     * @brief image reader's callback
     * image reader's callback
     * @param reader     the image reader
     */
    void image_callback(AImageReader* reader);
    /**
     * @brief display image
     * present camera image to the given display buffer, avaliable image is
     * converted to display buffer format, supported display format:
     *      WINDOW_FORMAT_RGBX_8888
     *      WINDOW_FORMAT_RGBA_8888
     * @param buf       for image to display to
     * @param img       a instance, source of image conversion.
     *                  it will be deleted via {@link AImage_delete}
     * @return          true on success, false on failure
     */
    bool display_image(ANativeWindow_Buffer* buf, AImage* img);
    /**
     * @brief configure the rotation angle
     * configure the rotation angle necessary to apply to camera image when presenting:
     *    all rotations should be accumulated:
     *    CameraSensorOrientation + Android Device Native Orientation +
     *    Human Rotation (rotated degree related to Phone native orientation
     * @param angle     the angle
     */
    void set_present_rotation(int32_t angle);
};


/**
 * @brief ncnn network
 * the ncnn network
 */
class NcnnNet
{
private:
    ncnn::Net* _network;            ///< the network
    bool _net_param_ready_flag;     ///< network parameter ready flag
    bool _net_model_ready_flag;     ///< network model ready flag
    bool _use_vkimagemat_flag;      ///< is the ncnn gpu device use VkImageMat enstore the data, if false, use VkMat
    int _default_gpu_index;         ///< default gpu index
    ncnn::VulkanDevice* _vkdev;     ///< vulkan device
    ncnn::VkCompute* _cmd;          ///< vulkan command
    ncnn::VkMat _in_rgb_mat;        ///< input rgb mat
    ncnn::VkImageMat _in_yuv_mat;   ///< input yuv mat

public:
    /**
     * @brief construction
     * construction function
     */
    NcnnNet(void);
    /**
     * @brief destruction
     * destruction function
     */
    ~NcnnNet(void);
    /**
     * @brief allocata the inupt data
     * allocata the inupt data of ncnn
     * @param in_hb     input data
     * @param in_res    input data format
     */
    void allocate_input(AHardwareBuffer* hb, ImageFormat res);
    /**
     * @brief load parameter
     * load parameter
     * @param mgr       android asset manager pointer
     * @param name      the name
     */
    void load_param(AAssetManager* mgr, const char* name);
    /**
     * @brief load model
     * load model
     * @param mgr       android asset manager pointer
     * @param name      the name
     */
    void load_model(AAssetManager* mgr, const char* name);
    /**
     * @brief detection
     * detection function
     * @param detect        detect if true, not detect if false
     * @return              the detect result
     */
    NcnnRet detect(bool detect);
};


/**
 * @brief application engine
 * the application engine
 */
class AppEngine
{
private:
    struct android_app* _app;           ///< android app pointer
    ImageFormat _native_win_res;        ///< saved native window
    int _rotation;                      ///< rotation
    NDKCamera* _cam;                    ///< camera
    NDKPicture* _pic;                   ///< RGB reader

    NcnnNet* _ncnn_net;                 ///< ncnn network
    bool _ncnn_detect_flag;             ///< ncnn detect flag
    bool _cam_granted_flag;             ///< granted camera or not

    bool _cam_ready_flag;               ///< camera ready flag
    bool _pic_ready_flag;               ///< granted bitmap picture or not

private:
    /**
     * @brief get the android app's display rotation
     * get the android app's display rotation
     * @return      the rotation
     */
    int get_display_rotation(void);

public:
    /**
     * @brief construction
     * construction function
     * @param app       android app
     */
    explicit AppEngine(android_app* app);
    /**
     * @brief destruction
     * destruction function
     */
    ~AppEngine(void);
    /**
     * @brief the interface to android app
     * the interface to android app
     * @return      android app pointer
     */
    struct android_app* interface_2_android_app(void) const;
    /**
     * @brief the interface for android asset manager
     * the interface for android asset manager
     * @param mgr       the android asset manager
     */
    void interface_4_aasset_mgr(AAssetManager* mgr);
    /**
     * @brief the interface for ncnn's input data
     * the interface for ncnn's input data
     * @param data      the image data
     * @param res       image resolution
     */
    void interface_4_ncnn_input(void* data, ImageFormat res);
    /**
     * @brief interface for ncnn's detection flag
     * interface for ncnn's detection flag
     */
    void interface_4_ncnn_detect(bool detect);
    /**
     * @brief handle Android System APP_CMD_INIT_WINDOW message
     * request camera persmission from Java side
     */
    void on_app_init_window(void);
    /**
     * @brief handle android app's request
     * configure the changes
     */
    void on_app_config_change(void);
    /**
     * @brief handle android app's request
     * terminate the native window
     */
    void on_app_term_window(void);
    /**
     * @brief get the native window width
     * get the native window width
     * @return      the width
     */
    int32_t get_native_win_width(void);
    /**
     * @brief get the native window height
     * get the native window height
     * @return      the height
     */
    int32_t get_native_win_height(void);
    /**
     * @brief get the native window format
     * get the native window format
     * @return      the format
     */
    int32_t get_native_win_format(void);
    /**
     * @brief get the native window resource
     * get the native window's width, height , format
     * @param w         width
     * @param h         height
     * @param format    the format
     */
    void set_native_win_res(int32_t w, int32_t h, int32_t format);
    /**
     * @brief request camera permissin
     * request camera permissin
     */
    void request_cam_permission(void);
    /**
     * @brief get camera permission
     * get camera permission
     * @param granted       is granted or not
     */
    void on_cam_permission(jboolean granted);
    /**
     * @brief enable the UI
     * enable the user interface
     */
    void enable_ui(void);
    /**
     * @brief craete the camera
     * craete the camera
     */
    void create_cam(void);
    /**
     * @brief craete the picture
     * craete the picture
     */
    void create_pic(void);
    /**
     * @brief delete the camera
     * delete the camera
     */
    void delete_cam(void);
    /**
     * @brief delete the picture
     * delete the picture
     */
    void delete_pic(void);
    /**
     * @brief draw the camera frame
     * draw the camera frame
     * @param detect_flag       detect flag
     */
    void draw_cam_frame(bool detect_flag);
    /**
     * @brief draw the pciture frame
     * draw the picture frame
     * @param detect_flag       detect flag
     */
    void draw_pic_frame(bool detect_flag);
};


#endif // MAIN_ACTIVITY_JNI_H