#include "main_activity_jni.h"


#define MAX_BUF_COUNT 4     ///< max buffers in this ImageReader


#ifndef PIXEL_MAX
#define PIXEL_MAX(a, b)     \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })
#endif // PIXEL_MAX
#ifndef PIXEL_MIN
#define PIXEL_MIN(a, b)     \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })
#endif // PIXEL_MIN


static const uint64_t k_min_exposure_time = static_cast<uint64_t>(1000000);
static const uint64_t k_max_exposure_time = static_cast<uint64_t>(250000000);
static const int k_max_channel_value = 262143;


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------common tools-------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
static inline uint32_t YUV2RGB(int n_y, int n_u, int n_v)
{
    n_y -= 16;
    n_u -= 128;
    n_v -= 128;
    if (n_y < 0)
        n_y = 0;

    int n_r = (int)(1192 * n_y + 1634 * n_v);
    int n_g = (int)(1192 * n_y - 833 * n_v - 400 * n_u);
    int n_b = (int)(1192 * n_y + 2066 * n_u);

    n_r = PIXEL_MAX(0, n_r);
    n_g = PIXEL_MAX(0, n_g);
    n_b = PIXEL_MAX(0, n_b);

    n_r = PIXEL_MIN(k_max_channel_value, n_r);
    n_g = PIXEL_MIN(k_max_channel_value, n_g);
    n_b = PIXEL_MIN(k_max_channel_value, n_b);

    n_r = (n_r >> 10) & 0xff;
    n_g = (n_g >> 10) & 0xff;
    n_b = (n_b >> 10) & 0xff;

    return 0xff000000 | (n_r << 16) | (n_g << 8) | n_b;
}


/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------camera/image listener---------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
void on_dev_state_changes(void* ctx, ACameraDevice* dev)
{
    reinterpret_cast<NDKCamera*>(ctx)->on_dev_state(dev);
}

void on_dev_error_changes(void* ctx, ACameraDevice* dev, int err)
{
    reinterpret_cast<NDKCamera*>(ctx)->on_dev_error(dev, err);
}

ACameraDevice_stateCallbacks* NDKCamera::get_dev_listener()
{
    static ACameraDevice_stateCallbacks cam_dev_listener = {
            .context = this,
            .onDisconnected = ::on_dev_state_changes,
            .onError = ::on_dev_error_changes,
    };
    return &cam_dev_listener;
}

void NDKCamera::on_dev_state(ACameraDevice* dev)
{
    // get ID
    std::string id(ACameraDevice_getId(dev));
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "device %s is disconnected", id.c_str());

    this->_cams[id].available_flag_ = false;
    ACameraDevice_close(this->_cams[id].device_);
    this->_cams.erase(id);
}

void NDKCamera::on_dev_error(ACameraDevice* dev, int err)
{
    // get ID
    std::string id(ACameraDevice_getId(dev));
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "camera device %s is in error %#x", id.c_str(), err);

    CameraId& cam = this->_cams[id];

    switch (err)
    {
        case ERROR_CAMERA_IN_USE:
            cam.available_flag_ = false;
            cam.own_flag_ = false;
            break;
        case ERROR_CAMERA_SERVICE:
        case ERROR_CAMERA_DEVICE:
        case ERROR_CAMERA_DISABLED:
        case ERROR_MAX_CAMERAS_IN_USE:
            cam.available_flag_ = false;
            cam.own_flag_ = false;
            break;
        default:
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "unknown camera device error: %#x", err);
    }
}

void on_cam_available(void* ctx, const char* id)
{
    reinterpret_cast<NDKCamera*>(ctx)->on_cam_status_changed(id, true);
}
void on_cam_unavailable(void* ctx, const char* id)
{
    reinterpret_cast<NDKCamera*>(ctx)->on_cam_status_changed(id, false);
}

ACameraManager_AvailabilityCallbacks* NDKCamera::get_mgr_listener()
{
    static ACameraManager_AvailabilityCallbacks cam_mgr_listener = {
            .context = this,
            .onCameraAvailable = ::on_cam_available,
            .onCameraUnavailable = ::on_cam_unavailable,
    };
    return &cam_mgr_listener;
}

void NDKCamera::on_cam_status_changed(const char *id, bool available)
{
    if (this->_valid_flag)
    {
        if (available)
        {
            this->_cams[std::string(id)].available_flag_ = true;
        }
        else
        {
            this->_cams[std::string(id)].available_flag_ = false;
        }
    }
}

void on_session_closed(void* ctx, ACameraCaptureSession* ses)
{
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "session %p closed", ses);
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::CLOSED);
}
void on_session_ready(void* ctx, ACameraCaptureSession* ses) {
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "session %p ready", ses);
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::READY);
}
void on_session_active(void* ctx, ACameraCaptureSession* ses)
{
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "session %p active", ses);
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::ACTIVE);
}

ACameraCaptureSession_stateCallbacks* NDKCamera::get_session_listener(void)
{
    static ACameraCaptureSession_stateCallbacks sessionListener = {
            .context = this,
            .onClosed = ::on_session_closed,
            .onReady = ::on_session_ready,
            .onActive = ::on_session_active,
    };
    return &sessionListener;
}

void NDKCamera::on_session_state(ACameraCaptureSession *ses, CaptureSessionState state)
{
    if (!ses || ses != this->_capture_session)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "capture session is %s", (ses ? "NOT our session" : "NULL"));
        return;
    }

    if (state >= CaptureSessionState::MAX_STATE)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "wrong state %d", state);
    }

    this->_capture_session_state = state;
}

void session_capture_callback_onfailed(void* context,
                                       ACameraCaptureSession* session,
                                       ACaptureRequest* request,
                                       ACameraCaptureFailure* failure)
{
    std::thread captureFailedThread(&NDKCamera::on_capture_failed,
                                    static_cast<NDKCamera*>(context),
                                    session,
                                    request,
                                    failure);
    captureFailedThread.detach();
}

void session_capture_callback_onsequenceend(void* context,
                                            ACameraCaptureSession* session,
                                            int sequence_id,
                                            int64_t frame_num)
{
    std::thread sequenceThread(&NDKCamera::on_capture_sequence_end,
                               static_cast<NDKCamera*>(context),
                               session,
                               sequence_id,
                               frame_num);
    sequenceThread.detach();
}
void session_capture_callback_onsequenceaborted(void* context,
                                                ACameraCaptureSession* session,
                                                int sequence_id)
{
    std::thread sequenceThread(&NDKCamera::on_capture_sequence_end,
                               static_cast<NDKCamera*>(context),
                               session,
                               sequence_id,
                               static_cast<int64_t>(-1));
    sequenceThread.detach();
}

ACameraCaptureSession_captureCallbacks* NDKCamera::get_capture_callback(void)
{
    static ACameraCaptureSession_captureCallbacks capture_listener{
            .context = this,
            .onCaptureStarted = nullptr,
            .onCaptureProgressed = nullptr,
            .onCaptureCompleted = nullptr,
            .onCaptureFailed = session_capture_callback_onfailed,
            .onCaptureSequenceCompleted = session_capture_callback_onsequenceend,
            .onCaptureSequenceAborted = session_capture_callback_onsequenceaborted,
            .onCaptureBufferLost = nullptr,
    };
    return &capture_listener;
}

void NDKCamera::on_capture_failed(ACameraCaptureSession *session, ACaptureRequest *request, ACameraCaptureFailure *failure)
{
    if (this->_valid_flag && request == this->_requests[JPG_CAPTURE_REQUEST_IDX].request_)
    {
        if (failure->sequenceId != this->_requests[JPG_CAPTURE_REQUEST_IDX].session_sequence_id_)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "Error jpg sequence id");
            this->start_preview(true);
        }
    }
}

void NDKCamera::on_capture_sequence_end(ACameraCaptureSession *session, int sequence_id, int64_t frame_num)
{
    if (sequence_id != this->_requests[JPG_CAPTURE_REQUEST_IDX].session_sequence_id_)
    {
        return;
    }

    ACameraCaptureSession_setRepeatingRequest(this->_capture_session,
                                              nullptr,
                                              1,
                                              &this->_requests[PREVIEW_REQUEST_IDX].request_,
                                              nullptr);
}

void on_image_callback(void *ctx, AImageReader *reader)
{
    reinterpret_cast<ImageReader *>(ctx)->image_callback(reader);
}


/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------camera manager----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
NDKCamera::NDKCamera()
        : _cam_mgr(nullptr),
          _active_cam_id(""),
          _cam_facing(ACAMERA_LENS_FACING_BACK),
          _cam_orientation(0),
          _output_container(nullptr),
          _capture_session_state(CaptureSessionState::MAX_STATE),
          _exposure_time(static_cast<int64_t>(0))
{
    camera_status_t status = ACAMERA_OK;

    this->_valid_flag = false;

    this->_requests.resize(CAPTURE_REQUEST_COUNT);
    memset(this->_requests.data(), 0, this->_requests.size() * sizeof(this->_requests[0]));

    // clear the cameras and be create the android camera manager
    this->_cams.clear();
    this->_cam_mgr = ACameraManager_create();
    if(this->_cam_mgr == nullptr)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "ACameraManager create failed! ");
        return;
    }

    // pick up a back facing camera
    enumerate_cam();
    if (this->_active_cam_id.size() == 0)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "unknown active camera index! ");
    }

    // create back facing camera device
    status = ACameraManager_openCamera(this->_cam_mgr,
                                       this->_active_cam_id.c_str(),
                                       this->get_dev_listener(),
                                       &this->_cams[this->_active_cam_id].device_);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't open the camera! ");
        return;
    }

    status = ACameraManager_registerAvailabilityCallback(this->_cam_mgr, this->get_mgr_listener());
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't register available callback! ");
        return;
    }

    // initialize camera controls (exposure time and sensitivity), pickup value of 2% * range + min as fixed value
    ACameraMetadata* acam_metadata;
    status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, this->_active_cam_id.c_str(), &acam_metadata);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get the camera characteristics! ");
        return;
    }
    ACameraMetadata_const_entry val = {0,};
    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE, &val);
    if (status == ACAMERA_OK)
    {
        this->_exposure_range.min_ = val.data.i64[0];
        if (this->_exposure_range.min_ < k_min_exposure_time)
        {
            this->_exposure_range.min_ = k_min_exposure_time;
        }
        this->_exposure_range.max_ = val.data.i64[1];
        if (this->_exposure_range.max_ > k_max_exposure_time)
        {
            this->_exposure_range.max_ = k_max_exposure_time;
        }
        this->_exposure_time = this->_exposure_range.value(50);
    }
    else
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "unsupported ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE");
        this->_exposure_range.min_ = this->_exposure_range.max_ = 0l;
        this->_exposure_time = 0l;
    }

    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &val);
    if (status == ACAMERA_OK)
    {
        this->_sensitivity_range.min_ = val.data.i32[0];
        this->_sensitivity_range.max_ = val.data.i32[1];
        this->_sensitivity = this->_sensitivity_range.value(50);
    }
    else
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed for ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE");
        this->_sensitivity_range.min_ = this->_sensitivity_range.max_ = 0;
        this->_sensitivity = 0;
    }

    // make the valid flag to be true
    this->_valid_flag = true;
}

void NDKCamera::enumerate_cam(void)
{
    camera_status_t status = ACAMERA_OK;

    // get android device's camera ID list
    ACameraIdList* cam_ids = nullptr;
    status = ACameraManager_getCameraIdList(this->_cam_mgr, &cam_ids);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get camera ID list! ");
        return;
    }

    // enumerate every camera
    for (int i = 0; i < cam_ids->numCameras; i++)
    {
        // current camera ID
        const char* id = cam_ids->cameraIds[i];

        // get android camera's metadata
        ACameraMetadata* acam_metadata;
        status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, id, &acam_metadata);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get camera metadata! ");
            return;
        }

        // find the camera facing back
        int32_t count = 0;
        const uint32_t* tags = nullptr;
        status = ACameraMetadata_getAllTags(acam_metadata, &count, &tags);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get camera metadata all tags! ");
            return;
        }
        // check the tags
        for (int tag_i = 0; tag_i < count; tag_i++)
        {
            if (ACAMERA_LENS_FACING == tags[tag_i])
            {
                ACameraMetadata_const_entry lens_info = {0,};
                status = ACameraMetadata_getConstEntry(acam_metadata, tags[tag_i], &lens_info);

                CameraId cam(id);
                cam.facing_ = static_cast<acamera_metadata_enum_android_lens_facing_t>(lens_info.data.u8[0]);
                cam.own_flag_ = false;
                cam.device_ = nullptr;
                this->_cams[cam.id_] = cam;
                if (cam.facing_ == ACAMERA_LENS_FACING_BACK)
                {
                    this->_active_cam_id = cam.id_;
                }
                break;
            }
        }
        // free the android camera meta data
        ACameraMetadata_free(acam_metadata);
    }

    if (this->_cams.size() == 0)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "no camera available on the device! ");
    }

    if (this->_active_cam_id.length() == 0)
    {
        // if no back facing camera found, pick up the first one to use...
        this->_active_cam_id = this->_cams.begin()->second.id_;
    }

    // delate the camera ID list
    ACameraManager_deleteCameraIdList(cam_ids);
}


bool NDKCamera::match_capture_size_request(ANativeWindow *display, ImageFormat *res_view, ImageFormat *res_cap)
{
    camera_status_t status = ACAMERA_OK;

    // get the display dimension
    DisplayDimension disp(ANativeWindow_getWidth(display), ANativeWindow_getHeight(display));
    if (this->_cam_orientation == 90 || this->_cam_orientation == 270)
    {
        disp.flip();
    }

    ACameraMetadata* acam_metadata;
    status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, this->_active_cam_id.c_str(), &acam_metadata);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get camera metadata! ");
        return false;
    }

    ACameraMetadata_const_entry entry;
    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get camera metadata const entry! ");
        return false;
    }

    // format of the data: format, width, height, input?, type int32
    bool found_it_flag = false;
    DisplayDimension found_res(4000, 4000);
    DisplayDimension max_jpg(0, 0);
    for (int i = 0; i < entry.count; i += 4)
    {
        int32_t input = entry.data.i32[i + 3];
        int32_t format = entry.data.i32[i + 0];
        if (input)
        {
            continue;
        }
        if (format == AIMAGE_FORMAT_YUV_420_888 || format == AIMAGE_FORMAT_JPEG)
        {
            DisplayDimension res(entry.data.i32[i + 1], entry.data.i32[i + 2]);
            if (!disp.IsSameRatio(res))
            {
                continue;
            }
            if (format == AIMAGE_FORMAT_YUV_420_888 && found_res > res)
            {
                found_it_flag = true;
                found_res = res;
            }
            else if (format == AIMAGE_FORMAT_JPEG && res > max_jpg)
            {
                max_jpg = res;
            }
        }
    }

    if (found_it_flag)
    {
        res_view->width = found_res.org_width();
        res_view->height = found_res.org_height();
        res_cap->width = max_jpg.org_width();
        res_cap->height = max_jpg.org_height();
    }
    else
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "did not find any compatible camera resolution, taking 640x480");
        if (disp.IsPortrait())
        {
            res_view->width = 480;
            res_view->height = 640;
        }
        else
        {
            res_view->width = 640;
            res_view->height = 480;
        }
        *res_cap = *res_view;
    }
    res_view->format = AIMAGE_FORMAT_YUV_420_888;
    res_cap->format = AIMAGE_FORMAT_JPEG;

    return found_it_flag;
}


void NDKCamera::create_session(ANativeWindow *preview_window, ANativeWindow *jpg_window, int32_t img_rotation)
{
    camera_status_t status = ACAMERA_OK;

    // create output from this app's ANativeWindow, and add into output container
    this->_requests[PREVIEW_REQUEST_IDX].output_native_window_ = preview_window;
    this->_requests[PREVIEW_REQUEST_IDX].template_ = TEMPLATE_PREVIEW;
    this->_requests[JPG_CAPTURE_REQUEST_IDX].output_native_window_ = jpg_window;
    this->_requests[JPG_CAPTURE_REQUEST_IDX].template_ = TEMPLATE_STILL_CAPTURE;

    // create the capture session's output container
    ACaptureSessionOutputContainer_create(&this->_output_container);
    for (auto& req : this->_requests)
    {
        // acquire the output
        ANativeWindow_acquire(req.output_native_window_);

        // create the capture session's output
        status = ACaptureSessionOutput_create(req.output_native_window_, &req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't create capture session output! ");
            return;
        }

        // add the capture session's output to the output container
        status = ACaptureSessionOutputContainer_add(this->_output_container, req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't add the output to container! ");
            return;
        }

        // create the camera output target
        status = ACameraOutputTarget_create(req.output_native_window_, &req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't create target! ");
            return;
        }

        // make the capture request
        status = ACameraDevice_createCaptureRequest(this->_cams[this->_active_cam_id].device_, req.template_, &req.request_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't create capture request! ");
            return;
        }

        //a add the target
        status = ACaptureRequest_addTarget(req.request_, req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't add target! ");
            return;
        }
    }

    // create a capture session for the given preview request
    this->_capture_session_state = CaptureSessionState::READY;
    status = ACameraDevice_createCaptureSession(this->_cams[this->_active_cam_id].device_,
                                                this->_output_container,
                                                this->get_session_listener(),
                                                &this->_capture_session);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't create capture session of canera device! ");
        return;
    }
    status = ACaptureRequest_setEntry_i32(this->_requests[JPG_CAPTURE_REQUEST_IDX].request_, ACAMERA_JPEG_ORIENTATION, 1, &img_rotation);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't set capture request! ");
        return;
    }

    // only preview request is in manual mode, JPG is always in auto mode
    // JPG capture mode could also be switch into manual mode and control
    // the capture parameters, this sample leaves JPG capture to be auto mode
    // (auto control has better effect than author's manual control)
    uint8_t ae_mode_off = ACAMERA_CONTROL_AE_MODE_OFF;
    status = ACaptureRequest_setEntry_u8(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_CONTROL_AE_MODE, 1, &ae_mode_off);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't set capture request! ");
        return;
    }
    status = ACaptureRequest_setEntry_i32(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_SENSOR_SENSITIVITY, 1, &this->_sensitivity);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't set capture request! ");
        return;
    }
    status = ACaptureRequest_setEntry_i64(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &this->_exposure_time);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't set capture request! ");
        return;
    }
}

bool NDKCamera::get_sensor_orientation(int32_t *facing, int32_t *angle)
{
    camera_status_t status = ACAMERA_OK;

    if (!this->_cam_mgr)
    {
        return false;
    }

    ACameraMetadata* acam_metadata;
    ACameraMetadata_const_entry face;
    ACameraMetadata_const_entry orientation;
    status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, this->_active_cam_id.c_str(), &acam_metadata);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get camera characteristics! ");
        return false;
    }
    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_LENS_FACING, &face);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get metadata const entry! ");
        return false;
    }
    this->_cam_facing = static_cast<int32_t>(face.data.u8[0]);

    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SENSOR_ORIENTATION, &orientation);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't get metadata const entry! ");
        return false;
    }
    this->_cam_orientation = orientation.data.i32[0];

    ACameraMetadata_free(acam_metadata);

    if (facing)
    {
        *facing = this->_cam_facing;
    }
    if (angle)
    {
        *angle = this->_cam_orientation;
    }

    return true;
}

void NDKCamera::start_preview(bool start)
{
    if (start)
    {
        ACameraCaptureSession_setRepeatingRequest(this->_capture_session,
                                                  nullptr,
                                                  1,
                                                  &this->_requests[PREVIEW_REQUEST_IDX].request_,
                                                  nullptr);
    }
    else if (!start && this->_capture_session_state == CaptureSessionState::ACTIVE)
    {
        ACameraCaptureSession_stopRepeating(this->_capture_session);
    }
    else
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "conflict states(%s, %d)", (start ? "true" : "false"), this->_capture_session_state);
    }
}

NDKCamera::~NDKCamera()
{
    camera_status_t status = ACAMERA_OK;

    this->_valid_flag = false;

    // stop session if it is on:
    if (this->_capture_session_state == CaptureSessionState::ACTIVE)
    {
        status = ACameraCaptureSession_stopRepeating(this->_capture_session);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't close capture session! ");
            return;
        }
    }
    ACameraCaptureSession_close(this->_capture_session);

    for (auto& req : this->_requests)
    {
        status = ACaptureRequest_removeTarget(req.request_, req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't remove target! ");
            return;
        }

        ACaptureRequest_free(req.request_);
        ACameraOutputTarget_free(req.target_);

        status = ACaptureSessionOutputContainer_remove(this->_output_container, req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't close capture output container! ");
            return;
        }

        ACaptureSessionOutput_free(req.session_output_);
        ANativeWindow_release(req.output_native_window_);
    }

    this->_requests.resize(0);
    ACaptureSessionOutputContainer_free(this->_output_container);

    for (auto& cam : this->_cams)
    {
        if (cam.second.device_)
        {
            status = ACameraDevice_close(cam.second.device_);
            if (status != ACAMERA_OK)
            {
                __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't close camera device! ");
                return;
            }
        }
    }
    this->_cams.clear();
    if (this->_cam_mgr)
    {
        status = ACameraManager_unregisterAvailabilityCallback(this->_cam_mgr, this->get_mgr_listener());
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't unregister availability callback! ");
            return;
        }
        ACameraManager_delete(this->_cam_mgr);
        this->_cam_mgr = nullptr;
    }
}


/*----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------image reader-----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
ImageReader::ImageReader(ImageFormat *res, enum AIMAGE_FORMATS format)
        : _present_rotation(0),
          _reader(nullptr)
{
    media_status_t status = AMEDIA_OK;

    this->_callback = nullptr;
    this->_callback_ctx = nullptr;

    status = AImageReader_new(res->width, res->height, format, MAX_BUF_COUNT, &this->_reader);
    if (status != AMEDIA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to create AImageReader");
    }

    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
    AImageReader_setImageListener(this->_reader, &listener);
}

void ImageReader::register_callback(void *ctx, std::function<void(void *, const char *)> func)
{
    this->_callback_ctx = ctx;
    this->_callback = func;
}

void ImageReader::image_callback(AImageReader *reader)
{
    media_status_t status = AMEDIA_OK;
    int32_t format;

    status = AImageReader_getFormat(reader, &format);
    if (status != AMEDIA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to get the media format");
    }

    if (format == AIMAGE_FORMAT_JPEG)
    {
        AImage *image = nullptr;
        status = AImageReader_acquireNextImage(reader, &image);
        if (status != AMEDIA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "image is not available");
        }
    }
}

ANativeWindow *ImageReader::get_native_window(void)
{
    media_status_t status = AMEDIA_OK;

    if (!this->_reader)
    {
        return nullptr;
    }

    ANativeWindow *native_window;
    status = AImageReader_getWindow(this->_reader, &native_window);
    if (status != AMEDIA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get ANativeWindow");
    }

    return native_window;
}

ImageFormat ImageReader::get_img_res(void)
{
    media_status_t status = AMEDIA_OK;

    ImageFormat img_fmt;
    int32_t width = 0;
    int32_t height = 0;
    int32_t format = 0;

    status = AImageReader_getWidth(this->_reader, &width);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get image format");
    status = AImageReader_getHeight(this->_reader, &height);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get image format");
    status = AImageReader_getFormat(this->_reader, &format);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get image format");

    if (format != AIMAGE_FORMAT_YUV_420_888)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to get format");

    img_fmt.width = width;
    img_fmt.height = height;
    img_fmt.format = format;

    return img_fmt;
}

AImage* ImageReader::get_next_img(void)
{
    media_status_t status = AMEDIA_OK;

    AImage *image;
    status = AImageReader_acquireNextImage(this->_reader, &image);
    if (status != AMEDIA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get next image");
        return nullptr;
    }

    return image;
}

AImage *ImageReader::get_latest_img(void)
{
    media_status_t status = AMEDIA_OK;

    AImage *image;
    status = AImageReader_acquireLatestImage(this->_reader, &image);
    if (status != AMEDIA_OK) {
        return nullptr;
    }

    return image;
}

void ImageReader::delete_img(AImage *img)
{
    if (img)
    {
        AImage_delete(img);
    }
}

bool ImageReader::display_image(ANativeWindow_Buffer *buf, AImage *img)
{
    if (buf->format != WINDOW_FORMAT_RGBX_8888 && buf->format != WINDOW_FORMAT_RGBA_8888)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "not supported buffer format");
    }

    int32_t src_format = -1;
    AImage_getFormat(img, &src_format);

    if (src_format != AIMAGE_FORMAT_YUV_420_888)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to get format");

    int32_t src_planes = 0;
    AImage_getNumberOfPlanes(img, &src_planes);
    if (src_planes != 3)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "it's not 3 planes");

    switch (this->_present_rotation)
    {
        case 0:
            this->present_img(buf, img);
            break;
        case 90:
            this->present_img90(buf, img);
            break;
        case 180:
            this->present_img180(buf, img);
            break;
        case 270:
            this->present_img270(buf, img);
            break;
        default:
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "not recognized display rotation: %d", this->_present_rotation);
    }

    AImage_delete(img);

    return true;
}

void ImageReader::present_img(ANativeWindow_Buffer *buf, AImage *img)
{
    AImageCropRect src_rect;
    AImage_getCropRect(img, &src_rect);

    int32_t y_stride, uv_stride;
    uint8_t *y_pixel, *u_pixel, *v_pixel;
    int32_t y_len, u_len, v_len;
    AImage_getPlaneRowStride(img, 0, &y_stride);
    AImage_getPlaneRowStride(img, 1, &uv_stride);
    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
    AImage_getPlaneData(img, 2, &u_pixel, &u_len);

    int32_t uv_pixel_stride;
    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);

    int32_t height = PIXEL_MIN(buf->height, (src_rect.bottom - src_rect.top));
    int32_t width = PIXEL_MIN(buf->width, (src_rect.right - src_rect.left));

    uint32_t *out = static_cast<uint32_t *>(buf->bits);
    for (int32_t y = 0; y < height; y++)
    {
        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;

        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);

        for (int32_t x = 0; x < width; x++)
        {
            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
            out[x] = YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
        }
        out += buf->stride;
    }
}

void ImageReader::present_img90(ANativeWindow_Buffer *buf, AImage *img)
{
    AImageCropRect src_rect;
    AImage_getCropRect(img, &src_rect);

    int32_t y_stride, uv_stride;
    uint8_t *y_pixel, *u_pixel, *v_pixel;
    int32_t y_len, u_len, v_len;
    AImage_getPlaneRowStride(img, 0, &y_stride);
    AImage_getPlaneRowStride(img, 1, &uv_stride);
    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
    AImage_getPlaneData(img, 2, &u_pixel, &u_len);

    int32_t uv_pixel_stride;
    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);

    int32_t height = PIXEL_MIN(buf->width, (src_rect.bottom - src_rect.top));
    int32_t width = PIXEL_MIN(buf->height, (src_rect.right - src_rect.left));

    uint32_t *out = static_cast<uint32_t *>(buf->bits);
    out += height - 1;
    for (int32_t y = 0; y < height; y++)
    {
        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;

        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);

        for (int32_t x = 0; x < width; x++)
        {
            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
            // [x, y]--> [-y, x]
            out[x * buf->stride] = YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
        }
        out -= 1;  // move to the next column
    }
}

void ImageReader::present_img180(ANativeWindow_Buffer *buf, AImage *img)
{
    AImageCropRect src_rect;
    AImage_getCropRect(img, &src_rect);

    int32_t y_stride, uv_stride;
    uint8_t *y_pixel, *u_pixel, *v_pixel;
    int32_t y_len, u_len, v_len;
    AImage_getPlaneRowStride(img, 0, &y_stride);
    AImage_getPlaneRowStride(img, 1, &uv_stride);
    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
    AImage_getPlaneData(img, 2, &u_pixel, &u_len);

    int32_t uv_pixel_stride;
    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);

    int32_t height = PIXEL_MIN(buf->height, (src_rect.bottom - src_rect.top));
    int32_t width = PIXEL_MIN(buf->width, (src_rect.right - src_rect.left));

    uint32_t *out = static_cast<uint32_t *>(buf->bits);
    out += (height - 1) * buf->stride;
    for (int32_t y = 0; y < height; y++)
    {
        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;

        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);

        for (int32_t x = 0; x < width; x++)
        {
            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
            // mirror image since we are using front camera
            out[width - 1 - x] = YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
            // out[x] = YUV2RGB(pY[x], pU[uv_offset], pV[uv_offset]);
        }
        out -= buf->stride;
    }
}

void ImageReader::present_img270(ANativeWindow_Buffer *buf, AImage *img)
{
    AImageCropRect src_rect;
    AImage_getCropRect(img, &src_rect);

    int32_t y_stride, uv_stride;
    uint8_t *y_pixel, *u_pixel, *v_pixel;
    int32_t y_len, u_len, v_len;
    AImage_getPlaneRowStride(img, 0, &y_stride);
    AImage_getPlaneRowStride(img, 1, &uv_stride);
    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
    AImage_getPlaneData(img, 2, &u_pixel, &u_len);

    int32_t uv_pixel_stride;
    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);

    int32_t height = PIXEL_MIN(buf->width, (src_rect.bottom - src_rect.top));
    int32_t width = PIXEL_MIN(buf->height, (src_rect.right - src_rect.left));

    uint32_t *out = static_cast<uint32_t *>(buf->bits);
    for (int32_t y = 0; y < height; y++)
    {
        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;

        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);

        for (int32_t x = 0; x < width; x++)
        {
            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
            out[(width - 1 - x) * buf->stride] =
                    YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
        }
        out += 1;  // move to the next column
    }
}

void ImageReader::set_present_rotation(int32_t angle)
{
    this->_present_rotation = angle;
}

ImageReader::~ImageReader()
{
    AImageReader_delete(this->_reader);
}


/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------camera engine-----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
NcnnNet::NcnnNet(void)
    : _network(nullptr),
      _net_param_ready_flag(false),
      _net_model_ready_flag(false),
      _use_vkimagemat_flag(true),
      _default_gpu_index(0),
      _vkdev(nullptr),
      _cmd(nullptr),
      _hb(nullptr),
      _vk_ahbi_allocator(nullptr)
{
    // initialize ncnn gpu instance
    ncnn::create_gpu_instance();

    // initialize the ncnn network
    _network = new ncnn::Net();

    // get ncnn vulkan gpu device
    this->_default_gpu_index = ncnn::get_default_gpu_index();
    this->_vkdev = ncnn::get_gpu_device();
    this->_cmd = new ncnn::VkCompute(this->_vkdev);

    // get the vulkan alloctor
    ncnn::VkAllocator* blob_vkallocator = this->_vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = this->_vkdev->acquire_staging_allocator();

    // make the options
    ncnn::Option opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;
    if (ncnn::get_gpu_count() != 0)
        opt.use_vulkan_compute = true;
    else
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "no vulkan device found! ");
        return;
    }
    this->_network->opt = opt;
}

void NcnnNet::init_hardwarebuffer(ImageReader* yuv_reader)
{
    int ret = -1;

    // initialize the android hardwarebuffer
    this->_hb_desc.width = yuv_reader->get_img_res().width;
    this->_hb_desc.height = yuv_reader->get_img_res().height;
    this->_hb_desc.format = yuv_reader->get_img_res().format;
    this->_hb_desc.rfu0 = 0;
    this->_hb_desc.rfu1 = 0;
    this->_hb_desc.layers = 1;
    this->_hb_desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

    ret = AHardwareBuffer_allocate(&this->_hb_desc, &this->_hb);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "can't allocate android hardwarebuffer %d", ret);



//     initalize the android vulkan allocator
//    this->_vk_ahbi_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, this->_hb);
}

void NcnnNet::load_param(AAssetManager *mgr, const char* name)
{
    int ret = this->_network->load_param(mgr, name);

    if (ret != 0)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "load parameter failed! ");
        return;
    }
    else
    {
        this->_net_param_ready_flag = true;
    }
}

void NcnnNet::load_model(AAssetManager *mgr, const char* name)
{
    int ret = this->_network->load_model(mgr, name);

    if (ret != 0)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "load model failed! ");
        return;
    }
    else
    {
        this->_net_model_ready_flag = true;
    }
}

void NcnnNet::update_vk_type(bool vkimagemat_flag)
{
    if (vkimagemat_flag)
        this->_use_vkimagemat_flag = true;
    else
        this->_use_vkimagemat_flag = false;
}

void NcnnNet::detect(bool flag)
{
    // check parameter and model
    if (!this->_net_param_ready_flag || !this->_net_model_ready_flag)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "didn't load the network's parameter or model yet! ");
    }
}

NcnnNet::~NcnnNet(void)
{
    // distory ncnn gpu instance
    ncnn::destroy_gpu_instance();
}


/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------camera engine-----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
AppEngine::AppEngine(android_app* app)
        : _app(app),
          _cam_granted_flag(false),
          _rotation(0),
          _cam_ready_flag(false),
          _cam(nullptr),
          _yuv_reader(nullptr),
          _jpg_reader(nullptr),
          _ncnn_net(nullptr),
          _ncnn_detect_flag(false)
{
    // get the memory to set the native window resolution
    memset(&this->_native_win_res, 0, sizeof(this->_native_win_res));
}

struct android_app* AppEngine::interface_4_android_app(void) const
{
    return this->_app;
}

void AppEngine::interface_2_aasset_mgr(AAssetManager* mgr)
{
    this->_ncnn_net->load_param(mgr, "squeezenet_v1.1.param");
    this->_ncnn_net->load_model(mgr, "squeezenet_v1.1.bin");
}

void AppEngine::interface_2_ncnn_vktype(bool vkimagemat_flag)
{
    this->_ncnn_net->update_vk_type(vkimagemat_flag);
}

void AppEngine::interface_2_ncnn_detectflag(void)
{
    if (this->_ncnn_detect_flag)
        this->_ncnn_detect_flag = false;
    else
        this->_ncnn_detect_flag = true;
}

void AppEngine::request_cam_permission(void)
{
    if (!this->_app)
        return;

    JNIEnv* env;
    ANativeActivity* activity = this->_app->activity;
    activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    activity->vm->AttachCurrentThread(&env, NULL);

    jobject activity_obj = env->NewGlobalRef(activity->clazz);
    jclass clz = env->GetObjectClass(activity_obj);
    env->CallVoidMethod(activity_obj, env->GetMethodID(clz, "RequestCamera", "()V"));
    env->DeleteGlobalRef(activity_obj);

    activity->vm->DetachCurrentThread();

    this->_cam_granted_flag = true;
}

void AppEngine::create_cam(void)
{
    // camera needed to be requested at the run-time from Java SDK, if not granted, do nothing
    if (!this->_cam_granted_flag || !this->_app->window)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "YOLOV5NCNNVULKAN", "camera is not granted! ");
        return;
    }

    // get the rotation we display
    int32_t display_rotation = this->get_display_rotation();
    this->_rotation = display_rotation;

    // make the NDK camera object
    this->_cam = new NDKCamera();

    // get some other information
    int32_t facing = 0;
    int32_t angle = 0;
    int32_t img_rotation = 0;

    if (this->_cam->get_sensor_orientation(&facing, &angle))
    {
        if (facing == ACAMERA_LENS_FACING_FRONT)
        {
            img_rotation = (angle + this->_rotation) % 360;
            img_rotation = (360 - img_rotation) % 360;
        }
        else
        {
            img_rotation = (angle - this->_rotation + 360) % 360;
        }
    }

    ImageFormat view{0, 0, 0};
    ImageFormat capture{0, 0, 0};
    this->_cam->match_capture_size_request(this->_app->window, &view, &capture);

    // request the necessary native window to OS
    bool portrait_nativewindow_flag = (this->_native_win_res.width < this->_native_win_res.height);

    ANativeWindow_setBuffersGeometry(this->_app->window,
                                     portrait_nativewindow_flag ? view.height : view.width,
                                     portrait_nativewindow_flag ? view.width : view.height,
                                     WINDOW_FORMAT_RGBA_8888);

    this->_yuv_reader = new ImageReader(&view, AIMAGE_FORMAT_YUV_420_888);
    this->_yuv_reader->set_present_rotation(img_rotation);
    this->_jpg_reader = new ImageReader(&capture, AIMAGE_FORMAT_JPEG);
    this->_jpg_reader->set_present_rotation(img_rotation);
//    this->jpg_reader_->register_callback(this, [this](void* ctx, const char* str)->void {reinterpret_cast<CameraEngine* >(ctx)->on_photo_taken(str);});

    // now we could create session
    this->_cam->create_session(this->_yuv_reader->get_native_window(), this->_jpg_reader->get_native_window(), img_rotation);
}

void AppEngine::draw_frame(void)
{
    if (!this->_cam_ready_flag || !this->_yuv_reader)
        return;

    AImage* image = this->_yuv_reader->get_next_img();
    if (!image)
        return;

    ANativeWindow_acquire(this->_app->window);
    ANativeWindow_Buffer buf;

    if (ANativeWindow_lock(this->_app->window, &buf, nullptr) < 0)
    {
        this->_yuv_reader->delete_img(image);
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "native window lock error! ");
        return;
    }

    this->_yuv_reader->display_image(&buf, image);
    ANativeWindow_unlockAndPost(this->_app->window);
    ANativeWindow_release(this->_app->window);
}

int AppEngine::get_display_rotation(void)
{
    JNIEnv *env;
    ANativeActivity *activity = this->_app->activity;
    activity->vm->GetEnv((void **)&env, JNI_VERSION_1_6);
    activity->vm->AttachCurrentThread(&env, NULL);

    jobject activity_obj = env->NewGlobalRef(activity->clazz);
    jclass clz = env->GetObjectClass(activity_obj);

    jint new_orientation = env->CallIntMethod(activity_obj, env->GetMethodID(clz, "getRotationDegree", "()I"));
    env->DeleteGlobalRef(activity_obj);

    activity->vm->DetachCurrentThread();

    return new_orientation;
}

void AppEngine::delete_cam(void)
{
    this->_cam_ready_flag = false;
    if (this->_cam)
    {
        delete this->_cam;
        this->_cam = nullptr;
    }
    if (this->_yuv_reader)
    {
        delete this->_yuv_reader;
        this->_yuv_reader = nullptr;
    }
    if (this->_jpg_reader)
    {
        delete this->_jpg_reader;
        this->_jpg_reader = nullptr;
    }
}

int32_t AppEngine::get_native_win_width(void)
{
    return this->_native_win_res.width;
}

int32_t AppEngine::get_native_win_height(void)
{
    return this->_native_win_res.height;
}

int32_t AppEngine::get_native_win_format(void)
{
    return this->_native_win_res.format;
}

void AppEngine::set_native_win_res(int32_t w, int32_t h, int32_t format)
{
    this->_native_win_res.width = w;
    this->_native_win_res.height = h;
    this->_native_win_res.format = format;
}

void AppEngine::on_app_init_window(void)
{
    if (!this->_cam_granted_flag)
    {
        // not permitted to use camera yet, ask again
        this->request_cam_permission();
    }

    this->_rotation = this->get_display_rotation();

    this->create_cam();

    this->enable_ui();

    // native activity is ready to display, start pulling images
    this->_cam_ready_flag = true;
    this->_cam->start_preview(true);
}

void AppEngine::on_app_init_ncnn(void)
{
    // initialize ncnn network
    this->_ncnn_net = new NcnnNet();
    this->_ncnn_net->init_hardwarebuffer(this->_yuv_reader);
}

void AppEngine::on_app_term_window(void)
{
    this->_cam_ready_flag = false;
    this->delete_cam();
}

void AppEngine::on_app_config_change(void)
{
    int new_rotation = this->get_display_rotation();

    if (new_rotation != this->_rotation) {
        this->on_app_term_window();

        this->_rotation = new_rotation;
        this->on_app_init_window();
    }
}

void AppEngine::on_cam_permission(jboolean granted)
{
    this->_cam_granted_flag = (granted != JNI_FALSE);

    if (this->_cam_granted_flag)
    {
        this->on_app_init_window();
    }
}


void AppEngine::enable_ui(void)
{
    JNIEnv *jni;
    this->_app->activity->vm->AttachCurrentThread(&jni, NULL);

    // default class retrieval
    jclass clazz = jni->GetObjectClass(this->_app->activity->clazz);
    jmethodID methodID = jni->GetMethodID(clazz, "EnableUI", "()V");
    jni->CallVoidMethod(this->_app->activity->clazz, methodID);
    this->_app->activity->vm->DetachCurrentThread();
}

AppEngine::~AppEngine(void)
{
    this->_cam_ready_flag = false;
    delete_cam();
}


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------native activity----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/


static AppEngine* p_engine_obj = nullptr;
static AAssetManager* mgr = nullptr;


AppEngine* get_app_engine(void)
{
    return p_engine_obj;
}

static void ProcessAndroidCmd(struct android_app* app, int32_t cmd)
{
    AppEngine* engine = reinterpret_cast<AppEngine*>(app->userData);
    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
            if (engine->interface_4_android_app()->window != NULL)
            {
                engine->set_native_win_res(ANativeWindow_getWidth(app->window),
                                           ANativeWindow_getHeight(app->window),
                                           ANativeWindow_getFormat(app->window));
                engine->on_app_init_window();
                engine->on_app_init_ncnn();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            engine->on_app_term_window();
            ANativeWindow_setBuffersGeometry(app->window,
                                             engine->get_native_win_width(),
                                             engine->get_native_win_height(),
                                             engine->get_native_win_format());
            break;
        case APP_CMD_CONFIG_CHANGED:
            engine->on_app_config_change();
            break;
        case APP_CMD_LOST_FOCUS:
            break;
    }
}


extern "C"
{
    void android_main(struct android_app* app)
    {
        AppEngine engine(app);

        p_engine_obj = &engine;

        app->userData = reinterpret_cast<void*>(&engine);
        app->onAppCmd = ProcessAndroidCmd;

        while (1)
        {
            // read all pending events
            int events;
            struct android_poll_source* source;

            while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0)
            {
                // process this event
                if (source != NULL)
                {
                    source->process(app, source);
                }

                // check if we are exiting
                if (app->destroyRequested != 0) {
                    engine.delete_cam();
                    p_engine_obj = nullptr;
                    return;
                }
            }
            p_engine_obj->draw_frame();
        }
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_UpateNcnnType(JNIEnv* env, jobject thiz, jboolean vkimagemat_flag)
    {
        if (vkimagemat_flag == JNI_TRUE)
            p_engine_obj->interface_2_ncnn_vktype(true);
        else
            p_engine_obj->interface_2_ncnn_vktype(false);
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NetworkInit(JNIEnv* env, jobject thiz, jobject assetManager)
    {
        mgr = AAssetManager_fromJava(env, assetManager);
        p_engine_obj->interface_2_aasset_mgr(mgr);
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NetworkDetect(JNIEnv* env, jobject thiz)
    {
        p_engine_obj->interface_2_ncnn_detectflag();
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NotifyCameraPermission(JNIEnv *env, jclass type, jboolean permission)
    {
        std::thread permission_handler(&AppEngine::on_cam_permission, get_app_engine(), permission);
        permission_handler.detach();
    }
}


