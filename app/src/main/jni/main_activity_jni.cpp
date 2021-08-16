#include "main_activity_jni.h"


#define MAX_BUF_COUNT 4     ///< max buffers in this ImageReader


static const uint64_t k_min_exposure_time = static_cast<uint64_t>(1000000);
static const uint64_t k_max_exposure_time = static_cast<uint64_t>(250000000);
static const int k_max_channel_value = 262143;


/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------- listener -------------------------------------------------*/
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
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_dev_state -> device %s is disconnected!", id.c_str());

    this->_cams[id].available_flag_ = false;
    ACameraDevice_close(this->_cams[id].device_);
    this->_cams.erase(id);
}

void NDKCamera::on_dev_error(ACameraDevice* dev, int err)
{
    // get ID
    std::string id(ACameraDevice_getId(dev));
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_dev_error -> %s = %#x", id.c_str(), err);

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
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_dev_error -> unknown camera device error!");
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
            this->_cams[std::string(id)].available_flag_ = true;
        else
            this->_cams[std::string(id)].available_flag_ = false;
    }
}

void on_session_closed(void* ctx, ACameraCaptureSession* ses)
{
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_closed -> error!");
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::CLOSED);
}
void on_session_ready(void* ctx, ACameraCaptureSession* ses) {
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_ready -> error!");
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::READY);
}
void on_session_active(void* ctx, ACameraCaptureSession* ses)
{
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_active -> error!");
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

void NDKCamera::image_callback(AImageReader *reader)
{
    media_status_t status = AMEDIA_OK;

    int32_t format;

    status = AImageReader_getFormat(reader, &format);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::image_callback AImageReader_getFormat -> %d", status);
}


void NDKCamera::on_session_state(ACameraCaptureSession *ses, CaptureSessionState state)
{
    if (!ses || ses != this->_capture_session)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_state -> capture session error!");
        return;
    }

    if (state >= CaptureSessionState::MAX_STATE)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_state -> capture session error!");

    this->_capture_session_state = state;
}

//void session_capture_callback_onfailed(void* context,
//                                       ACameraCaptureSession* session,
//                                       ACaptureRequest* request,
//                                       ACameraCaptureFailure* failure)
//{
//    std::thread captureFailedThread(&NDKCamera::on_capture_failed,
//                                    static_cast<NDKCamera*>(context),
//                                    session,
//                                    request,
//                                    failure);
//    captureFailedThread.detach();
//}
//
//void session_capture_callback_onsequenceend(void* context,
//                                            ACameraCaptureSession* session,
//                                            int sequence_id,
//                                            int64_t frame_num)
//{
//    std::thread sequenceThread(&NDKCamera::on_capture_sequence_end,
//                               static_cast<NDKCamera*>(context),
//                               session,
//                               sequence_id,
//                               frame_num);
//    sequenceThread.detach();
//}
//void session_capture_callback_onsequenceaborted(void* context,
//                                                ACameraCaptureSession* session,
//                                                int sequence_id)
//{
//    std::thread sequenceThread(&NDKCamera::on_capture_sequence_end,
//                               static_cast<NDKCamera*>(context),
//                               session,
//                               sequence_id,
//                               static_cast<int64_t>(-1));
//    sequenceThread.detach();
//}

//ACameraCaptureSession_captureCallbacks* NDKCamera::get_capture_callback(void)
//{
//    static ACameraCaptureSession_captureCallbacks capture_listener{
//            .context = this,
//            .onCaptureStarted = nullptr,
//            .onCaptureProgressed = nullptr,
//            .onCaptureCompleted = nullptr,
//            .onCaptureFailed = session_capture_callback_onfailed,
//            .onCaptureSequenceCompleted = session_capture_callback_onsequenceend,
//            .onCaptureSequenceAborted = session_capture_callback_onsequenceaborted,
//            .onCaptureBufferLost = nullptr,
//    };
//    return &capture_listener;
//}

//void NDKCamera::on_capture_failed(ACameraCaptureSession *session, ACaptureRequest *request, ACameraCaptureFailure *failure)
//{
//    if (this->_valid_flag && request == this->_requests[JPG_CAPTURE_REQUEST_IDX].request_)
//    {
//        if (failure->sequenceId != this->_requests[JPG_CAPTURE_REQUEST_IDX].session_sequence_id_)
//        {
//            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "Error jpg sequence id");
//            this->start_request(true);
//        }
//    }
//}
//
//void NDKCamera::on_capture_sequence_end(ACameraCaptureSession *session, int sequence_id, int64_t frame_num)
//{
//    if (sequence_id != this->_requests[JPG_CAPTURE_REQUEST_IDX].session_sequence_id_)
//    {
//        return;
//    }
//
//    ACameraCaptureSession_setRepeatingRequest(this->_capture_session,
//                                              nullptr,
//                                              1,
//                                              &this->_requests[PREVIEW_REQUEST_IDX].request_,
//                                              nullptr);
//}

void on_image_callback(void *ctx, AImageReader *reader)
{
//    reinterpret_cast<ImageReader *>(ctx)->image_callback(reader);
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
          _exposure_time(static_cast<int64_t>(0)),
          _native_window(nullptr),
          _callback(nullptr),
          _callback_ctx(nullptr)
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera ACameraManager_create -> %d", status);
        return;
    }

    // pick up a back facing camera
    enumerate_cam();
    if (this->_active_cam_id.size() == 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera -> no facing back camera!");

    // create back facing camera device
    status = ACameraManager_openCamera(this->_cam_mgr,
                                       this->_active_cam_id.c_str(),
                                       this->get_dev_listener(),
                                       &this->_cams[this->_active_cam_id].device_);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera ACameraManager_openCamera -> %d", status);
        return;
    }

    status = ACameraManager_registerAvailabilityCallback(this->_cam_mgr, this->get_mgr_listener());
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera ACameraManager_registerAvailabilityCallback -> %d", status);
        return;
    }

    // initialize camera controls (exposure time and sensitivity), pickup value of 2% * range + min as fixed value
    ACameraMetadata* acam_metadata;
    status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, this->_active_cam_id.c_str(), &acam_metadata);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera ACameraManager_getCameraCharacteristics -> %d", status);
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera -> unsupported ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE");
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::NDKCamera -> failed for ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE");
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::enumerate_cam ACameraManager_getCameraIdList -> %d", status);
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
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::enumerate_cam ACameraManager_getCameraCharacteristics -> %d", status);
            return;
        }

        // find the camera facing back
        int32_t count = 0;
        const uint32_t* tags = nullptr;
        status = ACameraMetadata_getAllTags(acam_metadata, &count, &tags);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::enumerate_cam ACameraMetadata_getAllTags -> %d", status);
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::enumerate_cam -> no camera available on the device! ");
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::match_capture_size_request ACameraManager_getCameraCharacteristics -> %d", status);
        return false;
    }

    ACameraMetadata_const_entry entry;
    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::match_capture_size_request ACameraMetadata_getConstEntry -> %d", status);
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::match_capture_size_request -> not any compatible camera resolution, taking 640x480!");
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


void NDKCamera::create_session(void)
{
    camera_status_t status = ACAMERA_OK;

    // create output from this app's ANativeWindow, and add into output container
    this->_requests[PREVIEW_REQUEST_IDX].output_native_window_ = this->_native_window;
    this->_requests[PREVIEW_REQUEST_IDX].template_ = TEMPLATE_PREVIEW;

    // create the capture session's output container
    ACaptureSessionOutputContainer_create(&this->_output_container);
    for (auto& req : this->_requests)
    {
        // acquire the output native window
        ANativeWindow_acquire(req.output_native_window_);

        // create the capture session's output
        status = ACaptureSessionOutput_create(req.output_native_window_, &req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureSessionOutput_create -> %d", status);
            return;
        }

        // add the capture session's output to the output container
        status = ACaptureSessionOutputContainer_add(this->_output_container, req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureSessionOutputContainer_add -> %d", status);
            return;
        }

        // create the camera output target
        status = ACameraOutputTarget_create(req.output_native_window_, &req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACameraOutputTarget_create -> %d", status);
            return;
        }

        // make the capture request
        status = ACameraDevice_createCaptureRequest(this->_cams[this->_active_cam_id].device_, req.template_, &req.request_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACameraDevice_createCaptureRequest -> %d", status);
            return;
        }

        // add the target
        status = ACaptureRequest_addTarget(req.request_, req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureRequest_addTarget -> %d", status);
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACameraDevice_createCaptureSession -> %d", status);
        return;
    }

    uint8_t ae_mode_off = ACAMERA_CONTROL_AE_MODE_OFF;
    status = ACaptureRequest_setEntry_u8(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_CONTROL_AE_MODE, 1, &ae_mode_off);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureRequest_setEntry_u8 -> %d", status);
        return;
    }
    status = ACaptureRequest_setEntry_i32(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_SENSOR_SENSITIVITY, 1, &this->_sensitivity);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureRequest_setEntry_i32 -> %d", status);
        return;
    }
    status = ACaptureRequest_setEntry_i64(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &this->_exposure_time);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureRequest_setEntry_i64 -> %d", status);
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::get_sensor_orientation ACameraManager_getCameraCharacteristics -> %d", status);
        return false;
    }
    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_LENS_FACING, &face);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::get_sensor_orientation ACameraMetadata_getConstEntry -> %d", status);
        return false;
    }
    this->_cam_facing = static_cast<int32_t>(face.data.u8[0]);

    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SENSOR_ORIENTATION, &orientation);
    if (status != ACAMERA_OK)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::get_sensor_orientation ACameraMetadata_getConstEntry -> %d", status);
        return false;
    }
    this->_cam_orientation = orientation.data.i32[0];

    ACameraMetadata_free(acam_metadata);

    if (facing)
        *facing = this->_cam_facing;

    if (angle)
        *angle = this->_cam_orientation;

    return true;
}

void NDKCamera::start_request(bool start)
{
    if (start)
        ACameraCaptureSession_setRepeatingRequest(this->_capture_session,
                                                  nullptr,
                                                  1,
                                                  &this->_requests[PREVIEW_REQUEST_IDX].request_,
                                                  nullptr);
    else if (!start && this->_capture_session_state == CaptureSessionState::ACTIVE)
        ACameraCaptureSession_stopRepeating(this->_capture_session);
    else
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::start_request -> conflict states! ");
}

void NDKCamera::init_img(ImageFormat res)
{
    media_status_t status = AMEDIA_OK;

    status = AImageReader_new(res.width, res.height, AIMAGE_FORMAT_YUV_420_888, MAX_BUF_COUNT, &this->_reader);
    if (status != AMEDIA_OK) {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::init_img AImageReader_new -> %d", status);
        return;
    }

    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
    AImageReader_setImageListener(this->_reader, &listener);

    ANativeWindow* native_window;

    status = AImageReader_getWindow(this->_reader, &native_window);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::init_img AImageReader_getWindow -> %d", status);

    this->_native_window = native_window;
}

AHardwareBuffer* NDKCamera::get_img_hb(void)
{
    int ret = -1;
    media_status_t status = AMEDIA_OK;

    AImage* image = nullptr;
    AHardwareBuffer* hb = nullptr;
    AHardwareBuffer_Desc hb_desc;

    hb_desc.width = this->_img_res.width;
    hb_desc.height = this->_img_res.height;
    hb_desc.format = this->_img_res.format;
    hb_desc.rfu0 = 0;
    hb_desc.rfu1 = 0;
    hb_desc.layers = 1;
    hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_MASK |
                    AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK |
                    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    ret = AHardwareBuffer_allocate(&hb_desc, &hb);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::get_img_hb AHardwareBuffer_allocate -> %d", ret);

    status = AImageReader_acquireNextImage(this->_reader, &image);
    if (status != AMEDIA_OK)
        return hb;

    status = AImage_getHardwareBuffer(image, &hb);
    if (status != AMEDIA_OK)
        return hb;

    return hb;
}

ImageFormat NDKCamera::get_img_res(void)
{
    media_status_t status = AMEDIA_OK;

    ImageFormat img_fmt;
    int32_t width = 0;
    int32_t height = 0;
    int32_t format = 0;

    status = AImageReader_getWidth(this->_reader, &width);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res AImageReader_getWidth -> %d", status);
    status = AImageReader_getHeight(this->_reader, &height);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res AImageReader_getHeight -> %d", status);
    status = AImageReader_getFormat(this->_reader, &format);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res AImageReader_getFormat -> %d", status);

    if (format != AIMAGE_FORMAT_YUV_420_888)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res -> wrong camera image format!");

    img_fmt.width = width;
    img_fmt.height = height;
    img_fmt.format = format;

    return img_fmt;
}

NDKCamera::~NDKCamera()
{
    camera_status_t status = ACAMERA_OK;

    this->_valid_flag = false;

    if (this->_capture_session_state == CaptureSessionState::ACTIVE)
    {
        status = ACameraCaptureSession_stopRepeating(this->_capture_session);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::~NDKCamera ACameraCaptureSession_stopRepeating -> %d", status);
            return;
        }
    }
    ACameraCaptureSession_close(this->_capture_session);

    for (auto& req : this->_requests)
    {
        status = ACaptureRequest_removeTarget(req.request_, req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::~NDKCamera ACaptureRequest_removeTarget -> %d", status);
            return;
        }

        ACaptureRequest_free(req.request_);
        ACameraOutputTarget_free(req.target_);

        status = ACaptureSessionOutputContainer_remove(this->_output_container, req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::~NDKCamera ACaptureSessionOutputContainer_remove -> %d", status);
            return;
        }

        ACaptureSessionOutput_free(req.session_output_);
        ANativeWindow_release(req.output_native_window_);

        AImageReader_delete(this->_reader);
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
                __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::~NDKCamera ACameraDevice_close -> %d", status);
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
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::~NDKCamera ACameraManager_unregisterAvailabilityCallback -> %d", status);
            return;
        }
        ACameraManager_delete(this->_cam_mgr);
        this->_cam_mgr = nullptr;
    }
}


///*----------------------------------------------------------------------------------------------------------*/
///*-----------------------------------------------image reader-----------------------------------------------*/
///*----------------------------------------------------------------------------------------------------------*/
//YUVImgReader::YUVImgReader(ImageFormat* res, enum AIMAGE_FORMATS format)
//    : _reader(nullptr),
//      _callback_ctx(nullptr),
//      _hb_desc(nullptr)
//{
//    media_status_t status = AMEDIA_OK;
//
//    this->_callback = nullptr;
//    this->_callback_ctx = nullptr;
//
//    status = AImageReader_new(res->width, res->height, format, MAX_BUF_COUNT, &this->_reader);
//    if (status != AMEDIA_OK) {
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::YUVImgReader AImageReader_new -> %d", status);
//        return;
//    }
//
//    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
//    AImageReader_setImageListener(this->_reader, &listener);
//}
//
//AHardwareBuffer* YUVImgReader::get_img_hb(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    AImage* image = nullptr;
//        AHardwareBuffer* hb = nullptr;
//
//    status = AImageReader_acquireNextImage(this->_reader, &image);
//    if (status != AMEDIA_OK)
//        return hb;
//
//    status = AImage_getHardwareBuffer(image, &hb);
//    if (status != AMEDIA_OK)
//        return hb;
//
//    return hb;
//}
//
//ImageFormat YUVImgReader::get_img_res(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    ImageFormat img_fmt;
//    int32_t width = 0;
//    int32_t height = 0;
//    int32_t format = 0;
//
//    status = AImageReader_getWidth(this->_reader, &width);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res AImageReader_getWidth -> %d", status);
//    status = AImageReader_getHeight(this->_reader, &height);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res AImageReader_getHeight -> %d", status);
//    status = AImageReader_getFormat(this->_reader, &format);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res AImageReader_getFormat -> %d", status);
//
//    if (format != AIMAGE_FORMAT_YUV_420_888)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_img_res -> wrong camera image format!");
//
//    img_fmt.width = width;
//    img_fmt.height = height;
//    img_fmt.format = format;
//
//    return img_fmt;
//}
//
//ANativeWindow* YUVImgReader::get_native_window(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    if (!this->_reader)
//        return nullptr;
//
//    ANativeWindow *native_window;
//
//    status = AImageReader_getWindow(this->_reader, &native_window);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::get_native_window AImageReader_getWindow -> %d", status);
//
//    return native_window;
//}
//
//void YUVImgReader::delete_img(AImage *img)
//{
//    if (img)
//        AImage_delete(img);
//}
//
//void YUVImgReader::image_callback(AImageReader *reader)
//{
//    media_status_t status = AMEDIA_OK;
//
//    int32_t format;
//
//    status = AImageReader_getFormat(reader, &format);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "YUVImgReader::image_callback AImageReader_getFormat -> %d", status);
//}
//
//YUVImgReader::~YUVImgReader()
//{
//    AImageReader_delete(this->_reader);
//}


NDKPicture::NDKPicture(void)
{
    this->_data = nullptr;
    this->_img_res = {};
}

void NDKPicture::init_img(void *data, ImageFormat img_res)
{
    this->_data = data;
    this->_img_res = img_res;
}

AHardwareBuffer* NDKPicture::get_img_hb(void)
{
    int ret = -1;
    AHardwareBuffer* hb = nullptr;
    AHardwareBuffer_Desc hb_desc;

    hb_desc.width = this->_img_res.width;
    hb_desc.height = this->_img_res.height;
    hb_desc.format = this->_img_res.format;
    hb_desc.rfu0 = 0;
    hb_desc.rfu1 = 0;
    hb_desc.layers = 1;
    hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_MASK |
                    AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK |
                    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    ret = AHardwareBuffer_allocate(&hb_desc, &hb);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "RGBImgReader::get_img AHardwareBuffer_allocate -> %d", ret);

    ret = AHardwareBuffer_lock(hb, AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK, 0, NULL, &this->_data);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "RGBImgReader::get_img AHardwareBuffer_lock -> %d", ret);
    AHardwareBuffer_unlock(hb, NULL);

    return hb;
}

ImageFormat NDKPicture::get_img_res(void)
{
    return this->_img_res;
}

NDKPicture::~NDKPicture(void)
{
    this->_data = nullptr;

    this->_img_res.width = {};
}




//ImageReader::ImageReader(ImageFormat *res, enum AIMAGE_FORMATS format)
//        : _present_rotation(0),
//          _reader(nullptr)
//{
//    media_status_t status = AMEDIA_OK;
//
//    this->_callback = nullptr;
//    this->_callback_ctx = nullptr;
//
//    status = AImageReader_new(res->width, res->height, format, MAX_BUF_COUNT, &this->_reader);
//    if (status != AMEDIA_OK)
//    {
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to create AImageReader ");
//        return;
//    }
//
//    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
//    AImageReader_setImageListener(this->_reader, &listener);
//}
//
//uint32_t YUVImgReader::yuv_2_rgb(int n_y, int n_u, int n_v)
//{
//    n_y -= 16;
//    n_u -= 128;
//    n_v -= 128;
//    if (n_y < 0)
//        n_y = 0;
//
//    int n_r = (int)(1192 * n_y + 1634 * n_v);
//    int n_g = (int)(1192 * n_y - 833 * n_v - 400 * n_u);
//    int n_b = (int)(1192 * n_y + 2066 * n_u);
//
//    n_r = n_r > 0 ? n_r : 0;
//    n_g = n_g > 0 ? n_g : 0;
//    n_b = n_b > 0 ? n_b : 0;
//
//    n_r = n_r < k_max_channel_value ? n_r : k_max_channel_value;
//    n_g = n_g < k_max_channel_value ? n_g : k_max_channel_value;
//    n_b = n_b < k_max_channel_value ? n_b : k_max_channel_value;
//
//    n_r = (n_r >> 10) & 0xff;
//    n_g = (n_g >> 10) & 0xff;
//    n_b = (n_b >> 10) & 0xff;
//
//    return 0xff000000 | (n_r << 16) | (n_g << 8) | n_b;
//}
//
//void ImageReader::image_callback(AImageReader *reader)
//{
//    media_status_t status = AMEDIA_OK;
//    int32_t format;
//
//    status = AImageReader_getFormat(reader, &format);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to get the media format");
//
//    if (format == AIMAGE_FORMAT_JPEG)
//    {
//        AImage *image = nullptr;
//        status = AImageReader_acquireNextImage(reader, &image);
//        if (status != AMEDIA_OK)
//            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "image is not available");
//    }
//}
//
//ANativeWindow *ImageReader::get_native_window(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    if (!this->_reader)
//        return nullptr;
//
//    ANativeWindow *native_window;
//    status = AImageReader_getWindow(this->_reader, &native_window);
//    if (status != AMEDIA_OK)
//    {
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get ANativeWindow");
//    }
//
//    return native_window;
//}
//
//ImageFormat ImageReader::get_img_res(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    ImageFormat img_fmt;
//    int32_t width = 0;
//    int32_t height = 0;
//    int32_t format = 0;
//
//    status = AImageReader_getWidth(this->_reader, &width);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get image format");
//    status = AImageReader_getHeight(this->_reader, &height);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get image format");
//    status = AImageReader_getFormat(this->_reader, &format);
//    if (status != AMEDIA_OK)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get image format");
//
//    if (format != AIMAGE_FORMAT_YUV_420_888)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to get format");
//
//    img_fmt.width = width;
//    img_fmt.height = height;
//    img_fmt.format = format;
//
//    return img_fmt;
//}
//
//AImage* ImageReader::get_next_img(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    AImage *image;
//    status = AImageReader_acquireNextImage(this->_reader, &image);
//    if (status != AMEDIA_OK)
//    {
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "could not get next image");
//        return nullptr;
//    }
//
//    return image;
//}
//
//AImage *ImageReader::get_latest_img(void)
//{
//    media_status_t status = AMEDIA_OK;
//
//    AImage *image;
//    status = AImageReader_acquireLatestImage(this->_reader, &image);
//    if (status != AMEDIA_OK) {
//        return nullptr;
//    }
//
//    return image;
//}
//
//void ImageReader::delete_img(AImage *img)
//{
//    if (img)
//    {
//        AImage_delete(img);
//    }
//}
//
//bool ImageReader::display_image(ANativeWindow_Buffer *buf, AImage *img)
//{
//    if (buf->format != WINDOW_FORMAT_RGBX_8888 && buf->format != WINDOW_FORMAT_RGBA_8888)
//    {
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "not supported buffer format");
//    }
//
//    int32_t src_format = -1;
//    AImage_getFormat(img, &src_format);
//
//    if (src_format != AIMAGE_FORMAT_YUV_420_888)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "failed to get format");
//
//    int32_t src_planes = 0;
//    AImage_getNumberOfPlanes(img, &src_planes);
//    if (src_planes != 3)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "it's not 3 planes");
//
//    switch (this->_present_rotation)
//    {
//        case 0:
//            this->present_img(buf, img);
//            break;
//        case 90:
//            this->present_img90(buf, img);
//            break;
//        case 180:
//            this->present_img180(buf, img);
//            break;
//        case 270:
//            this->present_img270(buf, img);
//            break;
//        default:
//            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "not recognized display rotation: %d", this->_present_rotation);
//    }
//
//    AImage_delete(img);
//
//    return true;
//}
//
//void ImageReader::present_img(ANativeWindow_Buffer *buf, AImage *img)
//{
//    AImageCropRect src_rect;
//    AImage_getCropRect(img, &src_rect);
//
//    int32_t y_stride, uv_stride;
//    uint8_t *y_pixel, *u_pixel, *v_pixel;
//    int32_t y_len, u_len, v_len;
//    AImage_getPlaneRowStride(img, 0, &y_stride);
//    AImage_getPlaneRowStride(img, 1, &uv_stride);
//    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
//    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
//    AImage_getPlaneData(img, 2, &u_pixel, &u_len);
//
//    int32_t uv_pixel_stride;
//    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);
//
//    int32_t height = buf->height < (src_rect.bottom - src_rect.top) ? buf->height : (src_rect.bottom - src_rect.top);
//    int32_t width = buf->width < (src_rect.right - src_rect.left) ? buf->width : (src_rect.right - src_rect.left);
////    int32_t height = PIXEL_MIN(buf->height, (src_rect.bottom - src_rect.top));
////    int32_t width = PIXEL_MIN(buf->width, (src_rect.right - src_rect.left));
//
//    uint32_t *out = static_cast<uint32_t *>(buf->bits);
//    for (int32_t y = 0; y < height; y++)
//    {
//        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;
//
//        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
//        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
//        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);
//
//        for (int32_t x = 0; x < width; x++)
//        {
//            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
//            out[x] = YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
//        }
//        out += buf->stride;
//    }
//}
//
//void ImageReader::present_img90(ANativeWindow_Buffer *buf, AImage *img)
//{
////    AImageCropRect src_rect;
////    AImage_getCropRect(img, &src_rect);
////
////    int32_t y_stride, uv_stride;
////    uint8_t *y_pixel, *u_pixel, *v_pixel;
////    int32_t y_len, u_len, v_len;
////    AImage_getPlaneRowStride(img, 0, &y_stride);
////    AImage_getPlaneRowStride(img, 1, &uv_stride);
////    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
////    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
////    AImage_getPlaneData(img, 2, &u_pixel, &u_len);
////
////    int32_t uv_pixel_stride;
////    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);
////
////    int32_t height = PIXEL_MIN(buf->width, (src_rect.bottom - src_rect.top));
////    int32_t width = PIXEL_MIN(buf->height, (src_rect.right - src_rect.left));
////
////    uint32_t *out = static_cast<uint32_t *>(buf->bits);
////    out += height - 1;
////    for (int32_t y = 0; y < height; y++)
////    {
////        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;
////
////        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
////        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
////        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);
////
////        for (int32_t x = 0; x < width; x++)
////        {
////            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
////            // [x, y]--> [-y, x]
////            out[x * buf->stride] = YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
////        }
////        out -= 1;  // move to the next column
////    }
//}
//
//void ImageReader::present_img180(ANativeWindow_Buffer *buf, AImage *img)
//{
////    AImageCropRect src_rect;
////    AImage_getCropRect(img, &src_rect);
////
////    int32_t y_stride, uv_stride;
////    uint8_t *y_pixel, *u_pixel, *v_pixel;
////    int32_t y_len, u_len, v_len;
////    AImage_getPlaneRowStride(img, 0, &y_stride);
////    AImage_getPlaneRowStride(img, 1, &uv_stride);
////    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
////    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
////    AImage_getPlaneData(img, 2, &u_pixel, &u_len);
////
////    int32_t uv_pixel_stride;
////    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);
////
////    int32_t height = PIXEL_MIN(buf->height, (src_rect.bottom - src_rect.top));
////    int32_t width = PIXEL_MIN(buf->width, (src_rect.right - src_rect.left));
////
////    uint32_t *out = static_cast<uint32_t *>(buf->bits);
////    out += (height - 1) * buf->stride;
////    for (int32_t y = 0; y < height; y++)
////    {
////        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;
////
////        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
////        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
////        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);
////
////        for (int32_t x = 0; x < width; x++)
////        {
////            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
////            // mirror image since we are using front camera
////            out[width - 1 - x] = YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
////            // out[x] = YUV2RGB(pY[x], pU[uv_offset], pV[uv_offset]);
////        }
////        out -= buf->stride;
////    }
//}
//
//void ImageReader::present_img270(ANativeWindow_Buffer *buf, AImage *img)
//{
////    AImageCropRect src_rect;
////    AImage_getCropRect(img, &src_rect);
////
////    int32_t y_stride, uv_stride;
////    uint8_t *y_pixel, *u_pixel, *v_pixel;
////    int32_t y_len, u_len, v_len;
////    AImage_getPlaneRowStride(img, 0, &y_stride);
////    AImage_getPlaneRowStride(img, 1, &uv_stride);
////    AImage_getPlaneData(img, 0, &y_pixel, &y_len);
////    AImage_getPlaneData(img, 1, &v_pixel, &v_len);
////    AImage_getPlaneData(img, 2, &u_pixel, &u_len);
////
////    int32_t uv_pixel_stride;
////    AImage_getPlanePixelStride(img, 1, &uv_pixel_stride);
////
////    int32_t height = PIXEL_MIN(buf->width, (src_rect.bottom - src_rect.top));
////    int32_t width = PIXEL_MIN(buf->height, (src_rect.right - src_rect.left));
////
////    uint32_t *out = static_cast<uint32_t *>(buf->bits);
////    for (int32_t y = 0; y < height; y++)
////    {
////        const uint8_t *p_y = y_pixel + y_stride * (y + src_rect.top) + src_rect.left;
////
////        int32_t uv_row_start = uv_stride * ((y + src_rect.top) >> 1);
////        const uint8_t *pU = u_pixel + uv_row_start + (src_rect.left >> 1);
////        const uint8_t *pV = v_pixel + uv_row_start + (src_rect.left >> 1);
////
////        for (int32_t x = 0; x < width; x++)
////        {
////            const int32_t uv_offset = (x >> 1) * uv_pixel_stride;
////            out[(width - 1 - x) * buf->stride] =
////                    YUV2RGB(p_y[x], pU[uv_offset], pV[uv_offset]);
////        }
////        out += 1;  // move to the next column
////    }
//}
//
//void ImageReader::set_present_rotation(int32_t angle)
//{
//    this->_present_rotation = angle;
//}
//
//ImageReader::~ImageReader()
//{
//    AImageReader_delete(this->_reader);
//}


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
      _cmd(nullptr)
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
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NcnnNet::NcnnNet -> no vulkan device found!");
    this->_network->opt = opt;
}

void NcnnNet::allocate_input(AHardwareBuffer* hb, ImageFormat res)
{
    if (hb == nullptr)
        return;

    if (res.format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM)
    {
        ncnn::VkAndroidHardwareBufferImageAllocator* ahbi_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, hb);
        this->_in_rgb_mat = this->_in_rgb_mat.from_android_hardware_buffer(ahbi_allocator);
    }
    else if (res.format == AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420)
    {
        ncnn::VkAndroidHardwareBufferImageAllocator* ahbi_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, hb);
        this->_in_yuv_mat = this->_in_yuv_mat.from_android_hardware_buffer(ahbi_allocator);
    }

    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "A%d", this->_in_yuv_mat.w);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "B%d", this->_in_yuv_mat.h);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "C%d", this->_in_yuv_mat.c);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "D%d", this->_in_yuv_mat.dims);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "E%d", this->_in_yuv_mat.elemsize);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "F%d", this->_in_yuv_mat.elempack);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "G%d", this->_in_yuv_mat.total());
}

void NcnnNet::load_param(AAssetManager *mgr, const char* name)
{
    int ret = this->_network->load_param(mgr, name);

    if (ret != 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NcnnNet::load_model -> load parameter failed!");
    else
        this->_net_param_ready_flag = true;
}

void NcnnNet::load_model(AAssetManager *mgr, const char* name)
{
    int ret = this->_network->load_model(mgr, name);

    if (ret != 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NcnnNet::load_model -> load model failed!");
    else
        this->_net_model_ready_flag = true;
}

NcnnRet NcnnNet::detect(bool detect)
{
    NcnnRet ncnn_ret;




    if (detect)
    {
        // check parameter and model
        if (!this->_net_param_ready_flag || !this->_net_model_ready_flag)
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NcnnNet::detect -> didn't load model or parameter");
    }

    return ncnn_ret;
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
          _pic(nullptr),
          _ncnn_net(nullptr),
          _ncnn_detect_flag(false)
{
    memset(&this->_native_win_res, 0, sizeof(this->_native_win_res));
}

struct android_app* AppEngine::interface_2_android_app(void) const
{
    return this->_app;
}

void AppEngine::interface_4_aasset_mgr(AAssetManager* mgr)
{
    this->_ncnn_net->load_param(mgr, "squeezenet_v1.1.param");
    this->_ncnn_net->load_model(mgr, "squeezenet_v1.1.bin");
}

void AppEngine::interface_4_ncnn_input(void* data, ImageFormat res)
{
    AHardwareBuffer* hb = nullptr;
    ImageFormat img_res;

    if (res.format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM)
    {
        this->_pic->init_img(data, res);
        hb = this->_pic->get_img_hb();
        img_res = this->_pic->get_img_res();
        this->_ncnn_net->allocate_input(hb, img_res);
    }
    else if (res.format == AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420)
    {
        hb = this->_cam->get_img_hb();
        img_res = this->_cam->get_img_res();
        this->_ncnn_net->allocate_input(hb, img_res);
    }
}

void AppEngine::interface_4_ncnn_detect(bool detect)
{
    this->_ncnn_detect_flag = detect;
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
    if (!this->_cam_granted_flag)
        this->request_cam_permission();

    int32_t display_rotation = this->get_display_rotation();
    this->_rotation = display_rotation;

    this->_cam = new NDKCamera();

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
            img_rotation = (angle - this->_rotation + 360) % 360;
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
    this->_cam->init_img(view);
    this->_cam->create_session();
}

void AppEngine::delete_cam(void)
{
    this->_cam_ready_flag = false;
    if (this->_cam)
    {
        delete this->_cam;
        this->_cam = nullptr;
    }
}

void AppEngine::create_pic(void)
{
    this->_pic = new NDKPicture;
}

void AppEngine::delete_pic(void)
{
    this->_pic_ready_flag = false;
    if (this->_pic)
    {
        delete this->_pic;
        this->_pic = nullptr;
    }
}

void AppEngine::draw_cam_frame(bool detect_flag)
{
//    if (!this->_cam_granted_flag)
//        return;
//
//    this->_cam->start_request(true);
//    this->_rotation = this->get_display_rotation();
//    this->_cam_ready_flag = true;
//
//    AHardwareBuffer* hb;
//    ImageFormat img_res;
//
//    hb = this->_yuv_reader->get_img();
//    if (hb == nullptr)
//        return;
//    img_res = this->_yuv_reader->get_img_res();
//
//    NcnnRet ncnn_ret;
//    ncnn_ret = this->_ncnn_net->detect(this->_ncnn_detect_flag, hb, img_res);

////    AImage* image = this->_yuv_reader->get_next_img();
////    if (!image)
////        return;
////
//    ANativeWindow_acquire(this->_app->window);
//    ANativeWindow_Buffer buf;
//
////    if (ANativeWindow_lock(this->_app->window, &buf, nullptr) < 0)
////    {
////        this->_yuv_reader->delete_img(image);
////        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "native window lock error! ");
////        return;
////    }
//
////    this->_yuv_reader->display_image(&buf, image);
//    ANativeWindow_unlockAndPost(this->_app->window);
//    ANativeWindow_release(this->_app->window);
}

void AppEngine::draw_pic_frame(bool detect_flag)
{
//    if (this->_rgb_reader == nullptr)
//        return;
//
//    AHardwareBuffer* hb;
//    ImageFormat img_res;
//
//    hb = this->_rgb_reader->get_img();
//    if (hb == nullptr)
//        return;
//    img_res = this->_rgb_reader->get_img_res();
//
//    NcnnRet ncnn_ret;
//    ncnn_ret = this->_ncnn_net->detect(this->_ncnn_detect_flag, hb, img_res);

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
    this->create_cam();
    this->create_pic();

    this->enable_ui();
    this->_ncnn_net = new NcnnNet();

    if (!this->_cam_granted_flag)
        return;

    this->_cam->start_request(true);
    this->_rotation = this->get_display_rotation();
    this->_cam_ready_flag = true;
}

void AppEngine::on_app_term_window(void)
{
    this->delete_cam();
    this->delete_pic();
}

void AppEngine::on_app_config_change(void)
{
    int new_rotation = this->get_display_rotation();

    if (new_rotation != this->_rotation)
    {
        this->on_app_term_window();

        this->_rotation = new_rotation;
        this->on_app_init_window();
    }
}

void AppEngine::on_cam_permission(jboolean granted)
{
    this->_cam_granted_flag = (granted != JNI_FALSE);

    if (this->_cam_granted_flag)
        this->on_app_init_window();
}


void AppEngine::enable_ui(void)
{
    JNIEnv *jni;

    this->_app->activity->vm->AttachCurrentThread(&jni, NULL);
    jclass clazz = jni->GetObjectClass(this->_app->activity->clazz);
    jmethodID methodID = jni->GetMethodID(clazz, "EnableUI", "()V");
    jni->CallVoidMethod(this->_app->activity->clazz, methodID);
    this->_app->activity->vm->DetachCurrentThread();
}

AppEngine::~AppEngine(void)
{
    this->delete_cam();
    this->delete_pic();
}


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------native activity----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/


static AppEngine* p_engine_obj = nullptr;
static AAssetManager* mgr = nullptr;
static int app_source_type = APP_SOURCE_NONE;
static bool app_ncnn_detect_flag = false;


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
            if (engine->interface_2_android_app()->window != NULL)
            {
                engine->set_native_win_res(ANativeWindow_getWidth(app->window),
                                           ANativeWindow_getHeight(app->window),
                                           ANativeWindow_getFormat(app->window));
                engine->on_app_init_window();
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
            int events;
            struct android_poll_source* source;

            while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0)
            {
                if (source != NULL)
                {
                    source->process(app, source);
                }

                if (app->destroyRequested != 0)
                {
                    engine.delete_cam();
                    p_engine_obj = nullptr;
                    return;
                }
            }
            if (app_source_type == APP_SOURCE_CAMERA)
                p_engine_obj->draw_cam_frame(app_ncnn_detect_flag);
            else if (app_source_type == APP_SOURCE_PICTURE)
                p_engine_obj->draw_pic_frame(app_ncnn_detect_flag);
        }
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_ChooseAlbum(JNIEnv* env, jobject thiz, jobject bitmap)
    {
        app_source_type = APP_SOURCE_PICTURE;

        int ret = -1;
        void* data;
        AndroidBitmapInfo bitmap_info;
        ImageFormat img_res;

        ret = AndroidBitmap_lockPixels(env, bitmap, &data);
        if (ret < 0)
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "JNI ChooseAlbum AndroidBitmap_lockPixels -> %d", ret);

        ret = AndroidBitmap_getInfo(env, bitmap, &bitmap_info);
        if (ret < 0)
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "JNI ChooseAlbum AndroidBitmap_getInfo -> %d", ret);
        img_res.width = bitmap_info.width;
        img_res.height = bitmap_info.height;
        img_res.format = bitmap_info.format;

        p_engine_obj->interface_4_ncnn_input(data, img_res);

        AndroidBitmap_unlockPixels(env, bitmap);
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_ChooseCamera(JNIEnv* env, jobject thiz)
    {
        app_source_type = APP_SOURCE_CAMERA;

        void* data;
        ImageFormat img_res;

        img_res.format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;

        p_engine_obj->interface_4_ncnn_input(data, img_res);
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NetworkInit(JNIEnv* env, jobject thiz, jobject assetManager)
    {
        mgr = AAssetManager_fromJava(env, assetManager);
        p_engine_obj->interface_4_aasset_mgr(mgr);
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NetworkDetect(JNIEnv* env, jobject thiz)
    {
//        p_engine_obj->interface_2_ncnn_detectflag();
        if (app_ncnn_detect_flag)
            app_ncnn_detect_flag = false;
        else
            app_ncnn_detect_flag = true;
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NotifyCameraPermission(JNIEnv *env, jclass type, jboolean permission)
    {
        std::thread permission_handler(&AppEngine::on_cam_permission, get_app_engine(), permission);
        permission_handler.detach();
    }
}


