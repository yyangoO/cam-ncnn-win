#include "main_activity_jni.h"


#define MAX_BUF_COUNT 2     ///< max buffers in this ImageReader


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
    LOGE("NDKCamera::on_dev_error -> %s = %#x", id.c_str(), err);
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
            LOGE("NDKCamera::on_dev_error -> unknown camera device error!");
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

void AppEngine::image_callback(AImageReader *reader)
{
    media_status_t status = AMEDIA_OK;
    int32_t format;
    status = AImageReader_getFormat(reader, &format);
    ASSERT(!status, "NDKCamera::image_callback AImageReader_getFormat -> %d", status);
    if (format != AIMAGE_FORMAT_YUV_420_888)
    {
        LOGE("AppEngine::image_callback -> wrong format! ");
    }
}


void NDKCamera::on_session_state(ACameraCaptureSession *ses, CaptureSessionState state)
{
    if (!ses || ses != this->_capture_session)
    {
        LOGE("NDKCamera::on_session_state -> capture session error!");
        return;
    }
    if (state >= CaptureSessionState::MAX_STATE)
    {
        LOGE("NDKCamera::on_session_state -> MAX_STATE");
    }
    this->_capture_session_state = state;
}

void on_image_callback(void *ctx, AImageReader *reader);


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

    this->_cams.clear();
    this->_cam_mgr = ACameraManager_create();
    ASSERT(this->_cam_mgr, "NDKCamera::NDKCamera ACameraManager_create -> failed to create camera manager")

    this->enumerate_cam();
    if (this->_active_cam_id.size() == 0)
    {
        LOGW("NDKCamera::NDKCamera -> no facing back camera! ");
    }

    status = ACameraManager_openCamera(this->_cam_mgr,
                                       this->_active_cam_id.c_str(),
                                       this->get_dev_listener(),
                                       &this->_cams[this->_active_cam_id].device_);
    ASSERT(!status, "NDKCamera::NDKCamera ACameraManager_openCamera -> %d", status)

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
    ASSERT(!status, "NDKCamera::enumerate_cam ACameraManager_getCameraIdList -> %d", status)

    for (int i = 0; i < cam_ids->numCameras; i++)
    {
        const char* id = cam_ids->cameraIds[i];

        ACameraMetadata* acam_metadata;
        status = ACameraManager_getCameraCharacteristics(this->_cam_mgr, id, &acam_metadata);
        ASSERT(!status, "NDKCamera::enumerate_cam ACameraManager_getCameraCharacteristics -> %d", status)

        int32_t count = 0;
        const uint32_t* tags = nullptr;
        status = ACameraMetadata_getAllTags(acam_metadata, &count, &tags);
        ASSERT(!status, "NDKCamera::enumerate_cam ACameraMetadata_getAllTags -> %d", status)

        for (int tag_i = 0; tag_i < count; tag_i++)
        {
            if (ACAMERA_LENS_FACING == tags[tag_i])
            {
                ACameraMetadata_const_entry lens_info = {0,};
                status = ACameraMetadata_getConstEntry(acam_metadata, tags[tag_i], &lens_info);
                ASSERT(!status, "NDKCamera::enumerate_cam ACameraMetadata_getConstEntry -> %d", status)

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
        LOGW("NDKCamera::enumerate_cam -> no camera available! ");
    }

    if (this->_active_cam_id.length() == 0)
    {
        this->_active_cam_id = this->_cams.begin()->second.id_;
    }

    ACameraManager_deleteCameraIdList(cam_ids);
}

bool NDKCamera::get_capture_size(ImageFormat* view)
{
    camera_status_t status = ACAMERA_OK;

    DisplayDimension disp(view->width, view->height);

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
        {
            continue;
        }
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
        LOGI("NDKCamera::get_capture_size -> no compatible camera resolution, taking default resolution! ");
        view->width = 640;
        view->height = 480;

    }
    view->format = AIMAGE_FORMAT_YUV_420_888;

    return found_it_flag;
}


void NDKCamera::create_session(ANativeWindow* native_window)
{
    camera_status_t status = ACAMERA_OK;

    this->_requests[PREVIEW_REQUEST_IDX].output_native_window_ = native_window;
    this->_requests[PREVIEW_REQUEST_IDX].template_ = TEMPLATE_PREVIEW;

    status = ACaptureSessionOutputContainer_create(&this->_output_container);
    ASSERT(!status, "NDKCamera::create_session ACaptureSessionOutputContainer_create -> %d", status);

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
    status = ACaptureRequest_setEntry_u8(this->_requests[PREVIEW_REQUEST_IDX].request_,
                                         ACAMERA_CONTROL_AE_MODE,
                                         1,
                                         &ae_mode_off);
    ASSERT(!status, "NDKCamera::create_session ACaptureRequest_setEntry_u8 -> %d", status);

    status = ACaptureRequest_setEntry_i32(this->_requests[PREVIEW_REQUEST_IDX].request_,
                                          ACAMERA_SENSOR_SENSITIVITY,
                                          1,
                                          &this->_sensitivity);
    ASSERT(!status, "NDKCamera::create_session ACaptureRequest_setEntry_i32 -> %d", status);

    status = ACaptureRequest_setEntry_i64(this->_requests[PREVIEW_REQUEST_IDX].request_,
                                          ACAMERA_SENSOR_EXPOSURE_TIME,
                                          1,
                                          &this->_exposure_time);
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

void NDKCamera::request(bool flag)
{
    camera_status_t status = ACAMERA_OK;
    if (flag)
    {
        status = ACameraCaptureSession_setRepeatingRequest(this->_capture_session,
                                                           nullptr,
                                                           1,
                                                           &this->_requests[PREVIEW_REQUEST_IDX].request_,
                                                           nullptr);
        ASSERT(!status, "NDKCamera::request ACameraCaptureSession_setRepeatingRequest -> %d", status);

    }
    else if (!flag && this->_capture_session_state == CaptureSessionState::ACTIVE)
    {
        status = ACameraCaptureSession_stopRepeating(this->_capture_session);
        ASSERT(!status, "NDKCamera::request ACameraCaptureSession_stopRepeating -> %d", status);
    }
    else
    {
        LOGE("NDKCamera::request -> conflict states! ");
    }
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
AppEngine::AppEngine(void)
    : _surface_res({}),
      _camera_res({}),
      _camera(nullptr),
      _buffer(nullptr),
      _camera_ready_flag(false),
      _network(nullptr),
      _vkdev(nullptr),
      _cmd(nullptr),
      _render_pipline(nullptr),
      _reader(nullptr),
      _camera_nwin(nullptr),
      _surface_nwin(nullptr),
      _callback(nullptr),
      _callback_ctx(nullptr)
{
    this->_surface_res.format = AIMAGE_FORMAT_RGBA_8888;
    this->_camera_res.format = AIMAGE_FORMAT_YUV_420_888;

    ncnn::create_gpu_instance();
    _network = new ncnn::Net();
    this->_vkdev = ncnn::get_gpu_device();
    this->_cmd = new ncnn::VkCompute(this->_vkdev);
    ncnn::VkAllocator* blob_vkallocator = this->_vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = this->_vkdev->acquire_staging_allocator();
    ncnn::Option opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;
    opt.use_vulkan_compute = true;
    this->_network->opt = opt;
    this->_render_pipline = new ncnn::RenderAndroidNativeWindowPipeline(this->_vkdev);
}

void AppEngine::create_camera(void)
{
    this->_camera_res.width = 1920;
    this->_camera_res.height = 1080;

    this->_camera = new NDKCamera();

    int32_t facing = 0;
    int32_t angle = 0;

    this->_camera->get_sensor_orientation(&facing, &angle);
    if (facing == ACAMERA_LENS_FACING_BACK)
    {
        LOGI("AppEngine::create_camera -> camera faces back! ");
    }
    else
    {
        LOGI("AppEngine::create_camera -> camera faces not back! ");
    }

    LOGI("AppEngine::create_camera -> camera angle is %d", angle);

    this->_camera->get_capture_size(&this->_camera_res);
    LOGI("AppEngine::create_camera -> camera width is %d", this->_camera_res.width);
    LOGI("AppEngine::create_camera -> camera height is %d", this->_camera_res.height);

    media_status_t status = AMEDIA_OK;
    status = AImageReader_new(this->_camera_res.width, this->_camera_res.height, this->_camera_res.format, MAX_BUF_COUNT, &this->_reader);
    ASSERT(!status, "AppEngine::AppEngine AImageReader_new -> %d", status);
    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
    status = AImageReader_setImageListener(this->_reader, &listener);
    ASSERT(!status, "AppEngine::AppEngine AImageReader_setImageListener -> %d", status);
    status = AImageReader_getWindow(this->_reader, &this->_camera_nwin);
    ASSERT(!status, "NDKCamera::create_imagereader AImageReader_getWindow -> %d", status);
    ANativeWindow_acquire(this->_camera_nwin);

    this->_camera->create_session(this->_camera_nwin);
    this->_camera->request(true);

}

void AppEngine::delete_camera(void)
{
    this->_camera_ready_flag = false;
    if (this->_camera)
    {
        this->_camera->request(false);
        delete this->_camera;
        this->_camera = nullptr;
    }

    if (this->_reader)
    {
        AImageReader_delete(this->_reader);
        this->_reader = nullptr;
    }
    if (this->_camera_nwin)
    {
        ANativeWindow_release(this->_camera_nwin);
        this->_camera_nwin = nullptr;
    }
}

void AppEngine::set_disp_window(ANativeWindow* native_window)
{
    if (this->_surface_nwin)
    {
        ANativeWindow_release(this->_surface_nwin);
        this->_surface_nwin = nullptr;
    }
    this->_surface_nwin = native_window;
    ANativeWindow_acquire(this->_surface_nwin);

    this->_surface_res.width = ANativeWindow_getWidth(native_window);
    this->_surface_res.height = ANativeWindow_getHeight(native_window);
    ANativeWindow_setBuffersGeometry(this->_surface_nwin, this->_surface_res.width, this->_surface_res.height, WINDOW_FORMAT_RGBA_8888);

    this->_render_pipline->create(this->_surface_nwin, 1, 1, this->_camera_res.width, this->_camera_res.height, this->_network->opt);
}

void AppEngine::draw_surface(void)
{
    int32_t ret = -1;
    media_status_t status = AMEDIA_OK;

    AImage* image = nullptr;
    ImageFormat res;

    status = AImageReader_acquireNextImage(this->_reader, &image);
    ASSERT(!status, "AppEngine::draw_surface AImageReader_acquireLatestImage -> %d", status);

    status = AImage_getWidth(image, &res.width);
    ASSERT(!status, "AppEngine::draw_surface AImage_getWidth -> %d", status);

    status = AImage_getHeight(image, &res.height);
    ASSERT(!status, "AppEngine::draw_surface AImage_getHeight -> %d", status);

    status = AImage_getFormat(image, &res.format);
    ASSERT(!status, "AppEngine::draw_surface AImage_getFormat -> %d", status);

    if(this->_surface_nwin)
    {
        LOGI("camera/surface -> width: %d/%d | height: %d/%d | format: %d/%d",
             res.width,
             this->_surface_res.width,
             res.height,
             this->_surface_res.height,
             res.format,
             this->_surface_res.format);

        AHardwareBuffer* hb = nullptr;

        status = AImage_getHardwareBuffer(image, &hb);
        ASSERT(!status, "AppEngine::draw_surface AImage_getHardwareBuffer -> %d", status);

//        ncnn::VkImageMat in_mat(1920, 1080, 4, 1, 1, ahb_im_allocator);
//        ncnn::VkImageMat in_img_mat(1920, 1080, 4, 1, 1, &ahb_im_allocator);
//        in_img_mat.from_android_hardware_buffer(ahb_im_allocator);
//        ncnn::VkMat temp_mat;
//        temp_mat.create_like(in_img_mat, this->_vkdev->acquire_blob_allocator());
//        ncnn::VkImageMat temp_img_mat;
//        temp_img_mat.create_like(temp_img_mat, this->_vkdev->acquire_blob_allocator());
//        this->_cmd->record_import_android_hardware_buffer(im_pipline, in_img_mat, temp_img_mat);
//        this->_cmd->record_import_android_hardware_buffer(&im_pipline, in_img_mat, temp_mat);
//        this->_cmd->record_clone(temp_mat, temp_img_mat, this->_network->opt);
//        this->_cmd->record_render_android_native_window(&render_pipline, temp_img_mat);
//        this->_cmd->submit_and_wait();
//        this->_cmd->reset();

//        void* bk_img = malloc(this->_surface_res.width * this->_surface_res.height * 4);
//        uint32_t* bk_data = static_cast<uint32_t*>(bk_img);
//        for (int j = 0; j < this->_surface_res.width * this->_surface_res.height; j++)
//        {
//            bk_data[j] = 0xff00ff00;
//        }
//
//        ANativeWindow_Buffer nativewindow_buffer;
//
//        ANativeWindow_lock(this->_surface_nwin, &nativewindow_buffer, nullptr);


//        ncnn::VkAndroidHardwareBufferImageAllocator ahb_im_allocator(this->_vkdev, hb);
//        ncnn::ImportAndroidHardwareBufferPipeline im_pipline(this->_vkdev);
//        im_pipline.create(&ahb_im_allocator, 4, 5, this->_surface_res.width, this->_surface_res.height, this->_network->opt);

//        ncnn::RenderAndroidNativeWindowPipeline render_pipline(this->_vkdev);
//        render_pipline.create(this->_surface_nwin, 1, 1, this->_surface_res.width, this->_surface_res.height, this->_network->opt);


//        memcpy(nativewindow_buffer.bits, bk_img, (this->_surface_res.width * this->_surface_res.height * 4));
//        ANativeWindow_unlockAndPost(this->_surface_nwin);
    }

    AImage_delete(image);
}

AppEngine::~AppEngine(void)
{
    this->delete_camera();
    ncnn::destroy_gpu_instance();
}


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------native activity----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
static AppEngine* p_engine = nullptr;
static ncnn::Mutex lock;

void on_image_callback(void *ctx, AImageReader *reader)
{
    ncnn::MutexLockGuard g(lock);

    reinterpret_cast<AppEngine *>(ctx)->image_callback(reader);
    p_engine->draw_surface();
}

extern "C"
{
    JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved)
    {
        LOGI("JNI onload");
        p_engine = new AppEngine();
        return JNI_VERSION_1_4;
    }

    JNIEXPORT void JNI_OnUnload(JavaVM* vm, void* reserved)
    {
        LOGI("JNI onunload");
        delete p_engine;
        p_engine = nullptr;
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_openCamera(JNIEnv* env, jobject thiz)
    {
        p_engine->create_camera();
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_closeCamera(JNIEnv* env, jobject thiz)
    {
        p_engine->delete_camera();
    }

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_setOutputWindow(JNIEnv *env, jobject thiz, jobject surface)
    {
        ANativeWindow* native_window = ANativeWindow_fromSurface(env, surface);
        p_engine->set_disp_window(native_window);
    }
}


