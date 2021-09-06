#ifndef MAIN_ACTIVITY_JNI_H
#define MAIN_ACTIVITY_JNI_H


#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>
#include <android/hardware_buffer.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraMetadataTags.h>
#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>

#include <functional>
#include <thread>
#include <string>
#include <vector>
#include <map>

#include "layer.h"
#include "net.h"
#include "benchmark.h"

#include "jni.h"


#define LOG_TAG "CAM2NCNN2WIN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ASSERT(cond, fmt, ...)                                          \
    if (!(cond))                                                        \
    {                                                                   \
        __android_log_assert(#cond, LOG_TAG, fmt, ##__VA_ARGS__);       \
    }


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
 * @brief camera capture session
 * initialize the NDK Camera2 CaptureSession state
 */
enum class CaptureSessionState : int32_t
{
    READY = 0,      ///< session is ready
    ACTIVE,         ///< session is busy
    CLOSED,         ///< session is closed (by itself or a new session evicts)
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


/**
 * @brief get the absolute value from relative value
 * this template return the absolute value from relative value
 */
template <typename T> class RangeValue
{
public:
    T min_;     ///< miminum value
    T max_;     ///< maximum value

public:
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
public:
    int32_t w_;             ///< width
    int32_t h_;             ///< height
    bool _portrait_flag;    ///< portrait
public:
    /**
     * @brief construct function
     * initialize the attributes of this class
     * @param w     the width
     * @param h     the height
     */
    DisplayDimension(int32_t w, int32_t h) : w_(w), h_(h), _portrait_flag(false)
    {
        if (h > w) {
            // make it landscape
            this->w_ = h;
            this->h_ = w;
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
        this->w_ = other.w_;
        this->h_ = other.h_;
        this->_portrait_flag = other._portrait_flag;
    }
    /**
     * @brief construct function
     * initialize the attributes of this class
     */
    DisplayDimension(void)
    {
        this->w_ = 0;
        this->h_ = 0;
        this->_portrait_flag = false;
    }
    /**
     * @brief reconstruct operator "="
     * reconstruct operator "="
     * @param other     other {@link DisplayDimension} object adress
     */
    DisplayDimension& operator=(const DisplayDimension& other)
    {
        this->w_ = other.w_;
        this->h_ = other.h_;
        this->_portrait_flag = other._portrait_flag;
        return (*this);
    }
    /**
     * @brief check if have same ratio or not
     * check if have same ratio or not
     * @param other     other {@link DisplayDimension} object adress
     * @return          true if same, false if not same
     */
    bool is_same_ratio(DisplayDimension& other)
    {
        return (this->w_ * other.h_ == this->h_ * other.w_);
    }
    /**
     * @brief check if it is bigger than other
     * check if it is bigger than other
     * @param other     other {@link DisplayDimension} object adress
     * @return          true if same, false if not same
     */
    bool operator>(DisplayDimension& other)
    {
        return (this->w_ >= other.w_ && this->h_ >= other.h_);
    }
    /**
     * @brief check if it is equal with other
     * check if it is equal with other
     * @param other     other {@link DisplayDimension} object adress
     * @return          true if same, false if not same
     */
    bool operator==(DisplayDimension& other)
    {
        return (this->w_ == other.w_ && this->h_ == other.h_ && this->_portrait_flag == other._portrait_flag);
    }
    /**
     * @brief get the short of two value
     * get the short of two value
     * @param other     other {@link DisplayDimension} object adress
     * @return          the result
     */
    DisplayDimension operator-(DisplayDimension& other)
    {
        DisplayDimension delta(this->w_ - other.w_, this->h_ - other.h_);
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
    bool is_portrait(void)
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
        return this->w_;
    }
    /**
     * @brief get the height
     * get the height
     * @return      the height
     */
    int32_t height(void)
    {
        return this->h_;
    }
    /**
     * @brief origin by the width
     * origin by the width
     * @return      the width
     */
    int32_t org_width(void)
    {
        return (this->_portrait_flag ? this->h_ : this->w_);
    }
    /**
     * @brief origin by the height
     * origin by the height
     * @return      the height
     */
    int32_t org_height(void)
    {
        return (this->_portrait_flag ? this->w_ : this->h_);
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
     * @brief get the capture size we request
     * get the capture size we request
     * @param view          the resolution and format we want to view
     * @return              true on success, false on failure
     */
    bool get_capture_size(ImageFormat* view);
    /**
     * @brief create a capture session
     * create a capture session of camera
     * @param native_window     the native window we enstore image of camera
     */
    void create_session(ANativeWindow* native_window);
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
     * @param flag        start flag
     */
    void request(bool flag);
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
};


/**
 * @brief application engine
 * the application engine
 */
class AppEngine
{
private:
    ImageFormat _surface_res;                                           ///< window resolution
    ImageFormat _camera_res;                                            ///< camera resolution
    NDKCamera* _camera;                                                 ///< camera
    AHardwareBuffer* _buffer;                                           ///< camera image hardwarebuffer
    bool _camera_ready_flag;                                            ///< camera ready flag

    ANativeWindow* _camera_nwin;                                        ///< the native window we enstore camera
    ANativeWindow* _surface_nwin;                                       ///< the native window we display image

    AImageReader* _reader;                                              ///< the android image reader
    std::function<void(void *ctx, const char* fileName)> _callback;     ///< the callback function
    void* _callback_ctx;                                                ///< the callback function pointer

    ncnn::Net* _network;                                                ///< ncnn
    ncnn::VulkanDevice* _vkdev;                                         ///< vulkan device
    ncnn::VkCompute* _compute;                                          ///< vulkan compute command
    ncnn::VkRender* _render;                                            ///< vulkan render command

public:
    /**
     * @brief construction
     * construction function
     */
    AppEngine(void);
    /**
     * @brief destruction
     * destruction function
     */
    ~AppEngine(void);
    /**
     * @brief set the output nativie window
     * set the native window we want to display
     */
    void set_disp_window(ANativeWindow* native_window);
    /**
     * @brief draw the surface
     * draw the surface
     */
    void draw_surface(void);
    /**
     * @brief craete the camera
     * craete the camera
     */
    void create_camera(void);
    /**
     * @brief delete the camera
     * delete the camera
     */
    void delete_camera(void);
    /**
     * @brief image reader's callback
     * image reader's callback
     * @param reader     the image reader
     */
    void image_callback(AImageReader* reader);
};


#endif // MAIN_ACTIVITY_JNI_H