#include "main_activity_jni.h"


#define MAX_BUF_COUNT 4     ///< max buffers in this ImageReader


static const uint64_t k_min_exposure_time = static_cast<uint64_t>(1000000);
static const uint64_t k_max_exposure_time = static_cast<uint64_t>(250000000);


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
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_closed -> closed!");
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::CLOSED);
}
void on_session_ready(void* ctx, ACameraCaptureSession* ses) {
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_ready -> ready!");
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::READY);
}
void on_session_active(void* ctx, ACameraCaptureSession* ses)
{
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::on_session_active -> active!");
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


//void on_image_callback(void *ctx, AImageReader *reader)
//{
////    reinterpret_cast<ImageReader *>(ctx)->image_callback(reader);
//}


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
          _hb(nullptr),
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


bool NDKCamera::get_capture_size(ANativeWindow* display, ImageFormat* view)
{
    camera_status_t status = ACAMERA_OK;

    DisplayDimension disp(ANativeWindow_getWidth(display), ANativeWindow_getHeight(display));
    if (this->_cam_orientation == 90 || this->_cam_orientation == 270)
        disp.flip();

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

    bool found_it_flag = false;
    DisplayDimension found_res(4000, 4000);
    for (int i = 0; i < entry.count; i += 4)
    {
        int32_t input = entry.data.i32[i + 3];
        int32_t format = entry.data.i32[i + 0];
        if (input)
            continue;
        if (format == AIMAGE_FORMAT_YUV_420_888)
        {
            DisplayDimension res(entry.data.i32[i + 1], entry.data.i32[i + 2]);
            if (!disp.is_same_ratio(res))
                continue;

            found_it_flag = true;
            found_res = res;
        }
    }

    if (found_it_flag)
    {
        view->width = found_res.org_width();
        view->height = found_res.org_height();
    }
    else
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::get_capture_size -> no compatible camera resolution, taking 1920*1080!");
        if (disp.IsPortrait())
        {
            view->width = 1080;
            view->height = 1920;
        }
        else
        {
            view->width = 1920;
            view->height = 1080;
        }
    }
    view->format = AIMAGE_FORMAT_YUV_420_888;

    return found_it_flag;
}


void NDKCamera::create_session(void)
{
    camera_status_t status = ACAMERA_OK;

    if (this->_native_window == nullptr)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session -> native window is not initialized!");
    }

    this->_requests[PREVIEW_REQUEST_IDX].output_native_window_ = this->_native_window;
    this->_requests[PREVIEW_REQUEST_IDX].template_ = TEMPLATE_PREVIEW;

    ACaptureSessionOutputContainer_create(&this->_output_container);
    for (auto& req : this->_requests)
    {
        ANativeWindow_acquire(req.output_native_window_);

        status = ACaptureSessionOutput_create(req.output_native_window_, &req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureSessionOutput_create -> %d", status);
            return;
        }

        status = ACaptureSessionOutputContainer_add(this->_output_container, req.session_output_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureSessionOutputContainer_add -> %d", status);
            return;
        }

        status = ACameraOutputTarget_create(req.output_native_window_, &req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACameraOutputTarget_create -> %d", status);
            return;
        }

        status = ACameraDevice_createCaptureRequest(this->_cams[this->_active_cam_id].device_, req.template_, &req.request_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACameraDevice_createCaptureRequest -> %d", status);
            return;
        }

        status = ACaptureRequest_addTarget(req.request_, req.target_);
        if (status != ACAMERA_OK)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::create_session ACaptureRequest_addTarget -> %d", status);
            return;
        }
    }

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

//    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
//    AImageReader_setImageListener(this->_reader, &listener);

    ANativeWindow* native_window;

    status = AImageReader_getWindow(this->_reader, &native_window);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKCamera::init_img AImageReader_getWindow -> %d", status);

    this->_native_window = native_window;
}

AImage* NDKCamera::get_next_img()
{
    media_status_t status = AMEDIA_OK;

    AImage *image;
    status = AImageReader_acquireNextImage(this->_reader, &image);
    if (status != AMEDIA_OK)
        return nullptr;

    return image;
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


/*----------------------------------------------------------------------------------------------------------*/
/*------------------------------------------------App engine------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
AppEngine::AppEngine(android_app* app)
        : _app(app),
          _cam_granted_flag(false),
          _rotation(0),
          _cam(nullptr),
          _cam_ready_flag(false),
          _win_hb(nullptr),
          _bk_img(nullptr),
          _network(nullptr),
          _vkdev(nullptr),
          _cmd(nullptr)
{
    memset(&this->_native_win_res, 0, sizeof(this->_native_win_res));

    this->_img_res.width = 1080;
    this->_img_res.height = 1920;
    this->_img_res.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    this->_bk_img = malloc(_img_res.width * _img_res.height * 4);
    uint32_t* bk_data = static_cast<uint32_t*>(this->_bk_img);
    for (int i = 0; i < _img_res.width * _img_res.height; i++)
        bk_data[i] = 0x0000ff00;

    this->init_background();

    ncnn::create_gpu_instance();
    _network = new ncnn::Net();
    this->_vkdev = ncnn::get_gpu_device();
    this->_cmd = new ncnn::VkCompute(this->_vkdev);
    ncnn::VkAllocator* blob_vkallocator = this->_vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = this->_vkdev->acquire_staging_allocator();
    ncnn::Option opt;
    opt.num_threads = 4;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;
    opt.use_vulkan_compute = true;
    this->_network->opt = opt;
}

struct android_app* AppEngine::interface_2_android_app(void) const
{
    return this->_app;
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

    if (!this->_app->window)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::create_cam -> app's window is not initialized!");
        return;
    }

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
    this->_cam->get_capture_size(this->_app->window, &view);


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

void AppEngine::show_camera(void)
{
    int32_t ret = -1;
    media_status_t status = AMEDIA_OK;

    AImage* image = nullptr;
    ImageFormat res;
    image = this->_cam->get_next_img();

    if (image == nullptr)
        return;

    status = AImage_getWidth(image, &res.width);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::show_camera AImage_getWidth -> %d", status);
    status = AImage_getHeight(image, &res.height);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::show_camera AImage_getHeight -> %d", status);
    status = AImage_getFormat(image, &res.format);
    if (status != AMEDIA_OK)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::show_camera AImage_getFormat -> %d", status);


    AHardwareBuffer* in_hb = nullptr;

    status = AImage_getHardwareBuffer(image, &in_hb);
    AHardwareBuffer_Desc in_hb_desc;

    AHardwareBuffer_describe(in_hb, &in_hb_desc);
    in_hb_desc.width = res.width;
    in_hb_desc.height = res.height;
    in_hb_desc.format = res.format;
    in_hb_desc.rfu0 = 0;
    in_hb_desc.rfu1 = 0;
    in_hb_desc.layers = 1;
    in_hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK;
    ret = AHardwareBuffer_allocate(&in_hb_desc, &in_hb);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_allocate -> %d", ret);




    ncnn::VkAndroidHardwareBufferImageAllocator* ahbi_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, this->_bk_hb);
    ncnn::vkMemoryAndroidHardwareBufferAllocator* mahb_allocator = new ncnn::vkMemoryAndroidHardwareBufferAllocator(this->_vkdev);


    ncnn::VkMat in_mat;
    in_mat.create(1080, 1920, 4, 1, 1, ahbi_allocator);
//    ncnn::VkImageMat in_img_mat;
//    in_img_mat.create(1080, 1920, 4, 1, 1, ahbi_allocator);

//    ncnn::VkMat temp_img_mat;
//    temp_img_mat.create(1080, 1920, 4, 1, 1, this->_vkdev->acquire_blob_allocator());
    ncnn::VkImageMat temp_img_mat;
    ncnn::VkImageMat out_img_mat;
//    out_img_mat.create(1080, 1920, 4, 1, 1, mahb_allocator);

//    ncnn::ImportAndroidHardwareBufferPipeline* im_pipeline = new ncnn::ImportAndroidHardwareBufferPipeline(this->_vkdev);
//    im_pipeline->create(ahbi_allocator, 4, 1, this->_network->opt);
//    ncnn::ExportAndroidHardwareBufferPipeline* ex_pipeline = new ncnn::ExportAndroidHardwareBufferPipeline(this->_vkdev);
//    ex_pipeline->create(mahb_allocator, 4, 1, this->_network->opt);

//    this->_cmd->record_import_android_hardware_buffer(im_pipeline, in_img_mat, temp_img_mat);
    this->_network->opt.blob_vkallocator = this->_vkdev->acquire_blob_allocator();
    this->_network->opt.staging_vkallocator = this->_vkdev->acquire_blob_allocator();
    this->_cmd->record_clone(in_mat, temp_img_mat, this->_network->opt);
    this->_network->opt.blob_vkallocator = mahb_allocator;
    this->_network->opt.staging_vkallocator = mahb_allocator;
    this->_cmd->record_clone(temp_img_mat, out_img_mat, this->_network->opt);
//    this->_cmd->record_export_android_hardware_buffer(ex_pipeline, temp_img_mat, out_img_mat);
    this->_cmd->submit_and_wait();
    this->_cmd->reset();



    AHardwareBuffer* out_hb = mahb_allocator->image_hb();

    AHardwareBuffer_Desc out_hb_desc;
    AHardwareBuffer_describe(out_hb, &out_hb_desc);

//    out_hb_desc.width = res.width;
//    out_hb_desc.height = res.height;
//    out_hb_desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
//    out_hb_desc.rfu0 = 0;
//    out_hb_desc.rfu1 = 0;
//    out_hb_desc.layers = 1;
//    out_hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
//    ret = AHardwareBuffer_allocate(&out_hb_desc, &out_hb);
//    if (ret < 0)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_allocate -> %d", ret);


    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AAAAAAAAAAAAAAAAAAA%d", out_hb_desc.width);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "BBBBBBBBBBBBBBBBBBB%d", out_hb_desc.height);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "CCCCCCCCCCCCCCCCCCC%d", out_hb_desc.format);
    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "DDDDDDDDDDDDDDDDDDD%d", out_hb_desc.usage == 0);




    void* out_data = nullptr;

    ANativeWindow_acquire(this->_app->window);
    ANativeWindow_Buffer nativewindow_buf;

    ret = ANativeWindow_lock(this->_app->window, &nativewindow_buf, nullptr);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::draw_frame ANativeWindow_lock -> %d", ret);

    AHardwareBuffer_acquire(out_hb);

    ret = AHardwareBuffer_lock(out_hb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &out_data);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::draw_frame AHardwareBuffer_lock -> %d", ret);

//    uint32_t* show_data = static_cast<uint32_t*>(this->_bk_img);
//    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX1");
//    for (int i = 0; i < 1920 * 1080; i++)
//    {
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "%d,    %d", i, show_data[i]);
//    }
//    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX1   %d", show_data[0]);
//    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX2   %d", show_data[1]);
//    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX3   %d", show_data[2]);
//    __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX4   %d", show_data[3]);
    memcpy(nativewindow_buf.bits, out_data, (this->_img_res.width * this->_img_res.height * 4));

    ret = AHardwareBuffer_unlock(out_hb, NULL);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::draw_frame AHardwareBuffer_lock -> %d", ret);

    AHardwareBuffer_release(out_hb);

    ret = ANativeWindow_unlockAndPost(this->_app->window);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKPicture::get_img_hb ANativeWindow_unlockAndPost -> %d", ret);

    ANativeWindow_release(this->_app->window);






//    im_pipeline->destroy();
//    delete im_pipeline;
//    delete ex_pipeline;
    delete ahbi_allocator;
    delete mahb_allocator;
}

void AppEngine::show_background()
{
    int32_t ret = -1;



    ncnn::VkAndroidHardwareBufferImageAllocator* ahbi_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, this->_bk_hb);
    ncnn::VkMat in_mat(1080, 1920, 4, 1, 1, ahbi_allocator);


    AHardwareBuffer* out_hb = nullptr;


    ncnn::VkMat out_mat;
    ncnn::vkMemoryAndroidHardwareBufferAllocator* mahb_allocator = new ncnn::vkMemoryAndroidHardwareBufferAllocator(this->_vkdev);



    this->_network->opt.blob_vkallocator = mahb_allocator;
    this->_cmd->record_clone(in_mat, out_mat, this->_network->opt);
    this->_cmd->submit_and_wait();
    this->_cmd->reset();

    out_hb = mahb_allocator->buffer_hb();


    void* out_data = nullptr;

    ANativeWindow_acquire(this->_app->window);
    ANativeWindow_Buffer nativewindow_buf;

    ret = ANativeWindow_lock(this->_app->window, &nativewindow_buf, nullptr);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::draw_frame ANativeWindow_lock -> %d", ret);

    AHardwareBuffer_acquire(out_hb);

    ret = AHardwareBuffer_lock(out_hb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &out_data);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::draw_frame AHardwareBuffer_lock -> %d", ret);

    memcpy(nativewindow_buf.bits, out_data, (this->_img_res.width * this->_img_res.height * 4));

    ret = AHardwareBuffer_unlock(out_hb, NULL);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::draw_frame AHardwareBuffer_lock -> %d", ret);

    AHardwareBuffer_release(out_hb);

    ret = ANativeWindow_unlockAndPost(this->_app->window);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "NDKPicture::get_img_hb ANativeWindow_unlockAndPost -> %d", ret);

    ANativeWindow_release(this->_app->window);


    delete ahbi_allocator;
}

void AppEngine::draw_frame(bool show_cam)
{
    if (!this->_app->window)
        return;

    if (!this->_cam_ready_flag)
        return;

    if (show_cam)
        this->show_camera();
    else
        this->show_background();
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
    if (!this->_cam_granted_flag)
        return;
    this->_cam->start_request(true);
    this->_cam_ready_flag = true;

    this->enable_ui();
}

void AppEngine::on_app_term_window(void)
{
    this->delete_cam();
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

void AppEngine::init_background(void)
{
    int32_t ret = -1;

    void* data = nullptr;

    AHardwareBuffer_Desc hb_desc;
    hb_desc.width = this->_img_res.width * this->_img_res.height * 4;
    hb_desc.height = 1;
    hb_desc.format = AHARDWAREBUFFER_FORMAT_BLOB;
    hb_desc.rfu0 = 0;
    hb_desc.rfu1 = 0;
    hb_desc.layers = 1;
    hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;
    ret = AHardwareBuffer_allocate(&hb_desc, &this->_bk_hb);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_allocate -> %d", ret);

    ret = AHardwareBuffer_lock(this->_bk_hb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &data);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_lock -> %d", ret);

    memcpy(data, this->_bk_img, (this->_img_res.width * this->_img_res.height * 4));

    ret = AHardwareBuffer_unlock(this->_bk_hb, NULL);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_unlock -> %d", ret);

}

AppEngine::~AppEngine(void)
{
    this->delete_cam();
    free(this->_bk_img);

    ncnn::destroy_gpu_instance();
}


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------native activity----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/


static AppEngine* p_engine_obj = nullptr;
static bool app_show_camera_flag = false;


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
            p_engine_obj->draw_frame(app_show_camera_flag);
        }
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_ChooseClean(JNIEnv* env, jobject thiz)
    {
        app_show_camera_flag = false;
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_ChooseCamera(JNIEnv* env, jobject thiz)
    {
        app_show_camera_flag = true;
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NotifyCameraPermission(JNIEnv *env, jclass type, jboolean permission)
    {
        std::thread permission_handler(&AppEngine::on_cam_permission, get_app_engine(), permission);
        permission_handler.detach();
    }
}


