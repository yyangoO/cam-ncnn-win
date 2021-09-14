#include "main_activity_jni.h"


#define LOG_TAG "CAM2NCNN2WIN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ASSERT(cond, fmt, ...)                                          \
    if (!(cond))                                                        \
    {                                                                   \
        __android_log_assert(#cond, LOG_TAG, fmt, ##__VA_ARGS__);       \
    }


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
    std::string id(ACameraDevice_getId(dev));
    LOGI("NDKCamera::on_dev_state -> device %s is disconnected!", id.c_str());

    this->_cams[id].available_flag_ = false;
    ACameraDevice_close(this->_cams[id].device_);
    this->_cams.erase(id);
}

void NDKCamera::on_dev_error(ACameraDevice* dev, int err)
{
    std::string id(ACameraDevice_getId(dev));
    LOGI("NDKCamera::on_dev_error -> %s = %#x", id.c_str(), err);

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
            LOGI("NDKCamera::on_dev_error -> unknown camera device error!");
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
    LOGI("NDKCamera::on_session_closed -> closed!");
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::CLOSED);
}

void on_session_ready(void* ctx, ACameraCaptureSession* ses) {
    LOGI("NDKCamera::on_session_ready -> ready!");
    reinterpret_cast<NDKCamera*>(ctx)->on_session_state(ses, CaptureSessionState::READY);
}

void on_session_active(void* ctx, ACameraCaptureSession* ses)
{
    LOGI("NDKCamera::on_session_active -> active!");
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
    ASSERT(!status, "NDKCamera::image_callback AImageReader_getFormat -> %d", status);
}

void NDKCamera::on_session_state(ACameraCaptureSession *ses, CaptureSessionState state)
{
    if (!ses || ses != this->_capture_session)
    {
        LOGI("NDKCamera::on_session_state -> capture session error!");
        return;
    }

    if (state >= CaptureSessionState::MAX_STATE)
    {
        LOGI("NDKCamera::on_session_state -> capture session error!");
    }

    this->_capture_session_state = state;
}

void on_image_callback(void *ctx, AImageReader *reader)
{
    reinterpret_cast<NDKCamera *>(ctx)->image_callback(reader);
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
          _hb(nullptr),
          _callback(nullptr),
          _callback_ctx(nullptr)
{
    camera_status_t status = ACAMERA_OK;

    this->_valid_flag = false;

    this->_requests.resize(CAPTURE_REQUEST_COUNT);
    memset(this->_requests.data(), 0, this->_requests.size() * sizeof(this->_requests[0]));

    this->_cams.clear();
    this->_cam_mgr = ACameraManager_create();
    if(this->_cam_mgr == nullptr)
    {
        LOGW("NDKCamera::NDKCamera ACameraManager_create -> %d", status);
        return;
    }

    enumerate_cam();
    if (this->_active_cam_id.size() == 0) {
        LOGW("NDKCamera::NDKCamera -> no facing back camera!");
    }

    status = ACameraManager_openCamera(this->_cam_mgr,
                                       this->_active_cam_id.c_str(),
                                       this->get_dev_listener(),
                                       &this->_cams[this->_active_cam_id].device_);
    ASSERT(!status, "NDKCamera::NDKCamera ACameraManager_openCamera -> %d", status);

    status = ACameraManager_registerAvailabilityCallback(this->_cam_mgr, this->get_mgr_listener());
    ASSERT(!status, "NDKCamera::NDKCamera ACameraManager_registerAvailabilityCallback -> %d", status);

    ACameraMetadata* acam_metadata;
    status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, this->_active_cam_id.c_str(), &acam_metadata);
    ASSERT(!status, "NDKCamera::NDKCamera ACameraManager_getCameraCharacteristics -> %d", status);

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
        LOGW("NDKCamera::NDKCamera -> unsupported ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE");
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
        LOGW("NDKCamera::NDKCamera -> failed for ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE");
        this->_sensitivity_range.min_ = this->_sensitivity_range.max_ = 0;
        this->_sensitivity = 0;
    }

    this->_valid_flag = true;
}

void NDKCamera::enumerate_cam(void)
{
    camera_status_t status = ACAMERA_OK;

    ACameraIdList* cam_ids = nullptr;
    status = ACameraManager_getCameraIdList(this->_cam_mgr, &cam_ids);
    ASSERT(!status, "NDKCamera::enumerate_cam ACameraManager_getCameraIdList -> %d", status);

    for (int i = 0; i < cam_ids->numCameras; i++)
    {
        const char* id = cam_ids->cameraIds[i];

        ACameraMetadata* acam_metadata;
        status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, id, &acam_metadata);
        ASSERT(!status, "NDKCamera::enumerate_cam ACameraManager_getCameraCharacteristics -> %d", status);

        int32_t count = 0;
        const uint32_t* tags = nullptr;
        status = ACameraMetadata_getAllTags(acam_metadata, &count, &tags);
        ASSERT(!status, "NDKCamera::enumerate_cam ACameraMetadata_getAllTags -> %d", status);

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

        ACameraMetadata_free(acam_metadata);
    }

    if (this->_cams.size() == 0)
    {
        LOGW("NDKCamera::enumerate_cam -> no camera available on the device! ");
    }

    if (this->_active_cam_id.length() == 0)
    {
        this->_active_cam_id = this->_cams.begin()->second.id_;
    }

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
    ASSERT(!status, "NDKCamera::match_capture_size_request ACameraManager_getCameraCharacteristics -> %d", status);

    ACameraMetadata_const_entry entry;
    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    ASSERT(!status, "NDKCamera::match_capture_size_request ACameraMetadata_getConstEntry -> %d", status);

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
            {
                continue;
            }

            found_it_flag = true;
            found_res = res;

            if (disp == res)
            {
                break;
            }
        }
    }

    if (found_it_flag)
    {
        view->width = found_res.org_width();
        view->height = found_res.org_height();
    }
    else
    {
        LOGW("NDKCamera::get_capture_size -> no compatible camera resolution, taking 1920*1080!");
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
        LOGW("NDKCamera::create_session -> native window is not initialized!");
    }

    this->_requests[PREVIEW_REQUEST_IDX].output_native_window_ = this->_native_window;
    this->_requests[PREVIEW_REQUEST_IDX].template_ = TEMPLATE_PREVIEW;

    ACaptureSessionOutputContainer_create(&this->_output_container);
    for (auto& req : this->_requests)
    {
        ANativeWindow_acquire(req.output_native_window_);

        status = ACaptureSessionOutput_create(req.output_native_window_, &req.session_output_);
        ASSERT(!status, "NDKCamera::create_session ACaptureSessionOutput_create -> %d", status);

        status = ACaptureSessionOutputContainer_add(this->_output_container, req.session_output_);
        ASSERT(!status, "NDKCamera::create_session ACaptureSessionOutputContainer_add -> %d", status);

        status = ACameraOutputTarget_create(req.output_native_window_, &req.target_);
        ASSERT(!status, "NDKCamera::create_session ACameraOutputTarget_create -> %d", status);

        status = ACameraDevice_createCaptureRequest(this->_cams[this->_active_cam_id].device_, req.template_, &req.request_);
        ASSERT(!status, "NDKCamera::create_session ACameraDevice_createCaptureRequest -> %d", status);

        status = ACaptureRequest_addTarget(req.request_, req.target_);
        ASSERT(!status, "NDKCamera::create_session ACaptureRequest_addTarget -> %d", status);
    }

    this->_capture_session_state = CaptureSessionState::READY;
    status = ACameraDevice_createCaptureSession(this->_cams[this->_active_cam_id].device_,
                                                this->_output_container,
                                                this->get_session_listener(),
                                                &this->_capture_session);
    ASSERT(!status, "NDKCamera::create_session ACameraDevice_createCaptureSession -> %d", status);

    uint8_t ae_mode_off = ACAMERA_CONTROL_AE_MODE_OFF;
    status = ACaptureRequest_setEntry_u8(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_CONTROL_AE_MODE, 1, &ae_mode_off);
    ASSERT(!status, "NDKCamera::create_session ACaptureRequest_setEntry_u8 -> %d", status);

    status = ACaptureRequest_setEntry_i32(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_SENSOR_SENSITIVITY, 1, &this->_sensitivity);
    ASSERT(!status, "NDKCamera::create_session ACaptureRequest_setEntry_i32 -> %d", status);

    status = ACaptureRequest_setEntry_i64(this->_requests[PREVIEW_REQUEST_IDX].request_, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &this->_exposure_time);
    ASSERT(!status, "NDKCamera::create_session ACaptureRequest_setEntry_i64 -> %d", status);
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
    ASSERT(!status, "NDKCamera::get_sensor_orientation ACameraManager_getCameraCharacteristics -> %d", status);

    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_LENS_FACING, &face);
    ASSERT(!status, "NDKCamera::get_sensor_orientation ACameraMetadata_getConstEntry -> %d", status);

    this->_cam_facing = static_cast<int32_t>(face.data.u8[0]);

    status = ACameraMetadata_getConstEntry(acam_metadata, ACAMERA_SENSOR_ORIENTATION, &orientation);
    ASSERT(!status, "NDKCamera::get_sensor_orientation ACameraMetadata_getConstEntry -> %d", status);

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

void NDKCamera::start_request(bool start)
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
    else {
        LOGW("NDKCamera::start_request -> conflict states! ");
    }
}

void NDKCamera::init_img(ImageFormat res)
{
    media_status_t status = AMEDIA_OK;

    status = AImageReader_newWithUsage(res.width,
                                       res.height,
                                       AIMAGE_FORMAT_YUV_420_888,
                                       AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                                       AHARDWAREBUFFER_USAGE_CPU_READ_MASK |
                                       AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK,
                                       MAX_BUF_COUNT,
                                       &this->_reader);
    ASSERT(!status, "NDKCamera::init_img AImageReader_new -> %d", status);

    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
    AImageReader_setImageListener(this->_reader, &listener);

    ANativeWindow* native_window;

    status = AImageReader_getWindow(this->_reader, &native_window);
    ASSERT(!status, "NDKCamera::init_img AImageReader_getWindow -> %d", status);

    this->_native_window = native_window;
}

AImage* NDKCamera::get_next_img()
{
    media_status_t status = AMEDIA_OK;

    AImage *image;
    status = AImageReader_acquireNextImage(this->_reader, &image);
    if (status != AMEDIA_OK)
    {
        return nullptr;
    }

    return image;
}

NDKCamera::~NDKCamera()
{
    camera_status_t status = ACAMERA_OK;

    this->_valid_flag = false;

    if (this->_capture_session_state == CaptureSessionState::ACTIVE)
    {
        status = ACameraCaptureSession_stopRepeating(this->_capture_session);
        ASSERT(!status, "NDKCamera::~NDKCamera ACameraCaptureSession_stopRepeating -> %d", status);

    }
    ACameraCaptureSession_close(this->_capture_session);

    for (auto& req : this->_requests)
    {
        status = ACaptureRequest_removeTarget(req.request_, req.target_);
        ASSERT(!status, "NDKCamera::~NDKCamera ACaptureRequest_removeTarget -> %d", status);

        ACaptureRequest_free(req.request_);
        ACameraOutputTarget_free(req.target_);

        status = ACaptureSessionOutputContainer_remove(this->_output_container, req.session_output_);
        ASSERT(!status, "NDKCamera::~NDKCamera ACaptureSessionOutputContainer_remove -> %d", status);

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
            ASSERT(!status, "NDKCamera::~NDKCamera ACameraDevice_close -> %d", status);
        }
    }

    this->_cams.clear();

    if (this->_cam_mgr)
    {
        status = ACameraManager_unregisterAvailabilityCallback(this->_cam_mgr, this->get_mgr_listener());
        ASSERT(!status, "NDKCamera::~NDKCamera ACameraManager_unregisterAvailabilityCallback -> %d", status);

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
          _network(nullptr),
          _vkdev(nullptr),
          _compute_cmd(nullptr),
          _render_cmd(nullptr)
{
    memset(&this->_native_win_res, 0, sizeof(this->_native_win_res));

    this->_img_res.width = 1080;
    this->_img_res.height = 1920;
    this->_img_res.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    ncnn::create_gpu_instance();
    _network = new ncnn::Net();
    this->_vkdev = ncnn::get_gpu_device();
    this->_compute_cmd = new ncnn::VkCompute(this->_vkdev);
    this->_render_cmd = new ncnn::VkRender(this->_vkdev);
    ncnn::VkAllocator* blob_vkallocator = this->_vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = this->_vkdev->acquire_staging_allocator();
    ncnn::Option opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;
    opt.use_vulkan_compute = true;
    opt.use_image_storage = true;
    this->_network->opt = opt;
    LOGW("ncnn gpu instance created!");
}

struct android_app* AppEngine::interface_2_android_app(void) const
{
    return this->_app;
}

void AppEngine::request_cam_permission(void)
{
    if (!this->_app)
    {
        return;
    }

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

void AppEngine::create_camera(void)
{
    if (!this->_cam_granted_flag)
        this->request_cam_permission();

    if (!this->_app->window)
    {
        LOGW("AppEngine::create_cam -> app's window is not initialized!");
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
        {
            img_rotation = (angle - this->_rotation + 360) % 360;
        }
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

void AppEngine::delete_camera(void)
{
    this->_cam_ready_flag = false;
    if (this->_cam)
    {
        delete this->_cam;
        this->_cam = nullptr;
    }
}

void AppEngine::draw_frame(void)
{
    if (!this->_app->window)
    {
        return;
    }
    if (!this->_cam_ready_flag)
    {
        return;
    }

    int32_t ret = -1;
    media_status_t status = AMEDIA_OK;

    AImage* image = nullptr;
    ImageFormat res;
    image = this->_cam->get_next_img();
    if (image == nullptr)
    {
        return;
    }

    status = AImage_getWidth(image, &res.width);
    ASSERT(!status, "AppEngine::show_camera AImage_getWidth -> %d", status);
    status = AImage_getHeight(image, &res.height);
    ASSERT(!status, "AppEngine::show_camera AImage_getHeight -> %d", status);
    status = AImage_getFormat(image, &res.format);
    ASSERT(!status, "AppEngine::show_camera AImage_getFormat -> %d", status);

    AHardwareBuffer *hb = nullptr;
    AHardwareBuffer_Desc hb_desc;
//    status = AImage_getHardwareBuffer(image, &hb);
//    ASSERT(!status, "AppEngine::draw_frame AImage_getHardwareBuffer -> %d", status);


    void* bk_img = malloc(this->_img_res.width * this->_img_res.height * 3 / 2);
    uint8_t* bk_data = static_cast<uint8_t*>(bk_img);
    for (int i = 0; i < this->_img_res.width * this->_img_res.height * 3 / 2; i++)
        bk_data[i] = 0xff;

    hb_desc.width = this->_img_res.width;
    hb_desc.height = this->_img_res.height;
    hb_desc.format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
    hb_desc.rfu0 = 0;
    hb_desc.rfu1 = 0;
    hb_desc.layers = 1;
    hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    ret = AHardwareBuffer_allocate(&hb_desc, &hb);

    void* data;
    ret = AHardwareBuffer_lock(hb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &data);
    memcpy(data, bk_img, this->_img_res.width * this->_img_res.height * 3 / 2);
    ret = AHardwareBuffer_unlock(hb, NULL);

    if (this->_app->window)
    {
        AHardwareBuffer_acquire(hb);
        ncnn::VkAndroidHardwareBufferImageAllocator ahb_im_allocator(this->_vkdev, hb);
        ncnn::VkR8g8b8a8UnormImageAllocator r8g8b8a8unorm_allocator(this->_vkdev);

        ncnn::VkImageMat in_img_mat;
        in_img_mat.from_android_hardware_buffer(&ahb_im_allocator);

//        ncnn::VkImageMat temp_img_mat(this->_img_res.width, this->_img_res.height, 4, 16u, 4, this->_network->opt.blob_vkallocator);
//
//        ncnn::VkImageMat out_img_mat( this->_native_win_res.width, this->_native_win_res.height, 4, 4u, 4, &r8g8b8a8unorm_allocator);
//
//        ncnn::ImportAndroidHardwareBufferPipeline import_pipeline(this->_vkdev);
//        ncnn::Convert2R8g8b8a8UnormPipeline convert_pipline(this->_vkdev);
//        import_pipeline.create(&ahb_im_allocator, 4, 5, this->_native_win_res.width, this->_native_win_res.height, this->_network->opt);
//        convert_pipline.create(4, 1, this->_img_res.width, this->_img_res.height, this->_native_win_res.width, this->_native_win_res.height, this->_network->opt);
//
//        this->_compute_cmd->record_import_android_hardware_buffer(&import_pipeline, in_img_mat, temp_img_mat);
//        this->_compute_cmd->record_convert2_r8g8b8a8_image(&convert_pipline, temp_img_mat, out_img_mat);
//        this->_compute_cmd->submit_and_wait();
//        this->_compute_cmd->reset();
//
//        this->_render_cmd->record_image(out_img_mat);
//        this->_render_cmd->render();
//        this->_render_cmd->reset();

//        AHardwareBuffer_acquire(hb);
//        void* out_data = nullptr;
//        ret = AHardwareBuffer_lock(hb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &out_data);
//        uint32_t* show_data = static_cast<uint32_t*>(out_data);
//        LOGW("AAAAAAAAAAAAAAAAAAAAAAAHardwarebuffer %d", show_data[0]);
//        LOGW("AAAAAAAAAAAAAAAAAAAAAAAHardwarebuffer %d", show_data[100]);
//        LOGW("AAAAAAAAAAAAAAAAAAAAAAAHardwarebuffer %d", show_data[2000]);
//        ret = AHardwareBuffer_unlock(hb, NULL);
        AHardwareBuffer_release(hb);
    }

    AImage_delete(image);
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
    this->create_camera();

    if (!this->_cam_granted_flag)
    {
        return;
    }
    this->_cam->start_request(true);
    this->_cam_ready_flag = true;

    this->enable_ui();

    this->_render_cmd->create(this->_app->window);
}

void AppEngine::on_app_term_window(void)
{
    this->delete_camera();
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
    {
        this->on_app_init_window();
    }
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
    this->delete_camera();

    ncnn::destroy_gpu_instance();
}


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------native activity----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
static AppEngine* p_engine_obj = nullptr;

AppEngine* get_app_engine(void)
{
    return p_engine_obj;
}

static void process_android_cmd(struct android_app* app, int32_t cmd)
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
    app->onAppCmd = process_android_cmd;

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
                engine.delete_camera();
                p_engine_obj = nullptr;
                return;
            }
        }
        p_engine_obj->draw_frame();
    }
}

JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_NotifyCameraPermission(JNIEnv *env, jclass type, jboolean permission)
{
    std::thread permission_handler(&AppEngine::on_cam_permission, get_app_engine(), permission);
    permission_handler.detach();
}
}