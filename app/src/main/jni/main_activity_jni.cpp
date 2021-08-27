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
        LOGE("NDKCamera::on_session_state -> capture session error!");
        return;
    }
    if (state >= CaptureSessionState::MAX_STATE)
    {
        LOGE("NDKCamera::on_session_state -> capture session error!");
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
      _imagereader_nwin(nullptr),
      _callback(nullptr),
      _callback_ctx(nullptr)
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
    }
    view->format = AIMAGE_FORMAT_YUV_420_888;

    return found_it_flag;
}


void NDKCamera::create_session(void)
{
    camera_status_t status = ACAMERA_OK;

    if (this->_imagereader_nwin == nullptr)
    {
        LOGE("NDKCamera::create_session -> didn't initialize the image reader yet! ");
        return;
    }

    this->_requests[PREVIEW_REQUEST_IDX].output_native_window_ = this->_imagereader_nwin;
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

void NDKCamera::start_request(bool start)
{
    camera_status_t status = ACAMERA_OK;
    if (start)
    {
        status = ACameraCaptureSession_setRepeatingRequest(this->_capture_session,
                                                           nullptr,
                                                           1,
                                                           &this->_requests[PREVIEW_REQUEST_IDX].request_,
                                                           nullptr);
        ASSERT(!status, "NDKCamera::start_request ACameraCaptureSession_setRepeatingRequest -> %d", status);

    }
    else if (!start && this->_capture_session_state == CaptureSessionState::ACTIVE)
    {
        status = ACameraCaptureSession_stopRepeating(this->_capture_session);
        ASSERT(!status, "NDKCamera::start_request ACameraCaptureSession_stopRepeating -> %d", status);
    }
    else
    {
        LOGE("NDKCamera::start_request -> conflict states! ");
    }
}

void NDKCamera::create_imagereader(ImageFormat res)
{
    media_status_t status = AMEDIA_OK;

    status = AImageReader_new(res.width, res.height, AIMAGE_FORMAT_YUV_420_888, MAX_BUF_COUNT, &this->_reader);
    ASSERT(!status, "NDKCamera::create_imagereader AImageReader_new -> %d", status);

    AImageReader_ImageListener listener{.context = this, .onImageAvailable = on_image_callback,};
    AImageReader_setImageListener(this->_reader, &listener);

    ANativeWindow* native_window;
    status = AImageReader_getWindow(this->_reader, &native_window);
    ASSERT(!status, "NDKCamera::create_imagereader AImageReader_getWindow -> %d", status);

    this->_imagereader_nwin = native_window;
}

AImage* NDKCamera::get_image()
{
    media_status_t status = AMEDIA_OK;

    AImage *image;
    status = AImageReader_acquireNextImage(this->_reader, &image);
    ASSERT(!status, "NDKCamera::get_image AImageReader_acquireNextImage -> %d", status);

    if (status == AMEDIA_OK)
    {
        return image;
    }
    else
    {
        return nullptr;
    }
}

void NDKCamera::destory_imagereader(void)
{
    AImageReader_delete(this->_reader);
    ANativeWindow_release(this->_imagereader_nwin);
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
AppEngine::AppEngine(void)
    : _win_res({}),
      _img_res({}),
      _camera(nullptr),
      _buffer(nullptr),
      _camera_ready_flag(false),
      _native_window(nullptr),
      _network(nullptr),
      _vkdev(nullptr),
      _cmd(nullptr)
{
    memset(&this->_win_res, 0, sizeof(this->_win_res));
    memset(&this->_img_res, 0, sizeof(this->_img_res));

    this->_win_res.format = AIMAGE_FORMAT_YUV_420_888;
    this->_img_res.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    this->_bk_img = malloc(_img_res.width * _img_res.height * 4);
    uint32_t* bk_data = static_cast<uint32_t*>(this->_bk_img);
    for (int i = 0; i < _img_res.width * _img_res.height; i++)
    {
        bk_data[i] = 0x0000ff00;
    }

    ncnn::create_gpu_instance();
//    _network = new ncnn::Net();
//    this->_vkdev = ncnn::get_gpu_device();
//    this->_cmd = new ncnn::VkCompute(this->_vkdev);
//    ncnn::VkAllocator* blob_vkallocator = this->_vkdev->acquire_blob_allocator();
//    ncnn::VkAllocator* staging_vkallocator = this->_vkdev->acquire_staging_allocator();
//    ncnn::Option opt;
//    opt.num_threads = 4;
//    opt.blob_vkallocator = blob_vkallocator;
//    opt.workspace_vkallocator = blob_vkallocator;
//    opt.staging_vkallocator = staging_vkallocator;
//    opt.use_vulkan_compute = true;
//    this->_network->opt = opt;
//
//    AHardwareBuffer* in_hb;
//    ncnn::VkAndroidHardwareBufferImageAllocator* ahb_im_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, in_hb);
//
//    ncnn::ImportAndroidHardwareBufferPipeline* im_pipeline = new ncnn::ImportAndroidHardwareBufferPipeline(this->_vkdev);
//    im_pipeline->create(ahb_im_allocator, 1, 1, this->_network->opt);
//
//    ncnn::VkImageMat in_img_mat(1080, 1920, 3, 1, 1, ahb_im_allocator);
//    in_img_mat.from_android_hardware_buffer(ahb_im_allocator);
//    ncnn::VkMat temp_mat(1080, 1920, 4, 1, 1, blob_vkallocator);
//    ncnn::VkImageMat temp_img_mat;
//    temp_img_mat.create_like(in_img_mat, blob_vkallocator);
//
//    this->_cmd->record_import_android_hardware_buffer(im_pipeline, in_img_mat, temp_img_mat);
//    this->_cmd->submit_and_wait();
//    this->_cmd->reset();
}

void AppEngine::create_camera(int width, int height)
{
    this->_camera = new NDKCamera();

    int32_t facing = 0;
    int32_t angle = 0;

    this->_camera->get_sensor_orientation(&facing, &angle);
    if (facing == ACAMERA_LENS_FACING_FRONT)
    {
        LOGI("AppEngine::create_camera -> camera faces front! ");
    }
    else
    {
        LOGI("AppEngine::create_camera -> camera faces back! ");
    }

    LOGI("AppEngine::create_camera -> camera angle is %d", angle);

    ImageFormat camera_view{width, height, AIMAGE_FORMAT_YUV_420_888};
    this->_camera->get_capture_size(&camera_view);
    this->_camera->create_imagereader(camera_view);
    this->_camera->create_session();
    this->_img_res = camera_view;
}

void AppEngine::delete_camera(void)
{
    this->_camera_ready_flag = false;
    if (this->_camera)
    {
        delete this->_camera;
        this->_camera = nullptr;
    }
}

void AppEngine::set_disp_window(ANativeWindow* native_window)
{
    this->_native_window = native_window;
    this->_win_res.width = ANativeWindow_getWidth(native_window);
    this->_win_res.height = ANativeWindow_getHeight(native_window);
    this->_win_res.format = AIMAGE_FORMAT_RGBA_8888;
    ANativeWindow_setBuffersGeometry(this->_native_window, this->_win_res.width, this->_win_res.height, WINDOW_FORMAT_RGBA_8888);
}

void AppEngine::show_camera(void)
{
    int32_t ret = -1;
    media_status_t status = AMEDIA_OK;

    AImage* image = nullptr;
    ImageFormat res;
    image = this->_camera->get_image();

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
    AHardwareBuffer_Desc in_hb_desc;

    AHardwareBuffer_describe(in_hb, &in_hb_desc);
    in_hb_desc.width = res.width;
    in_hb_desc.height = res.height;
    in_hb_desc.format = res.format;
    in_hb_desc.rfu0 = 0;
    in_hb_desc.rfu1 = 0;
    in_hb_desc.layers = 1;
    in_hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK | AHARDWAREBUFFER_USAGE_CPU_READ_MASK | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    ret = AHardwareBuffer_allocate(&in_hb_desc, &in_hb);
    if (ret < 0)
        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_allocate -> %d", ret);


    status = AImage_getHardwareBuffer(image, &in_hb);
    ncnn::VkAndroidHardwareBufferImageAllocator* ahb_im_allocator = new ncnn::VkAndroidHardwareBufferImageAllocator(this->_vkdev, in_hb);


    AImage_delete(image);
}

void AppEngine::show_background()
{

}

//void AppEngine::draw_frame(bool show_cam)
//{
//    if (!this->_camera_ready_flag)
//        return;
//
//    this->show_camera();
//}
//
//
//
//void AppEngine::on_app_init_window(void)
//{
//    this->create_cam();
//    if (!this->_cam_granted_flag)
//        return;
//    this->_cam->start_request(true);
//    this->_cam_ready_flag = true;
//
//    this->enable_ui();
//}
//
//void AppEngine::on_app_term_window(void)
//{
//    this->delete_cam();
//}
//
//void AppEngine::on_app_config_change(void)
//{
//    int new_rotation = this->get_display_rotation();
//
//    if (new_rotation != this->_rotation)
//    {
//        this->on_app_term_window();
//        this->_rotation = new_rotation;
//        this->on_app_init_window();
//    }
//}
//
//void AppEngine::on_cam_permission(jboolean granted)
//{
//    this->_cam_granted_flag = (granted != JNI_FALSE);
//
//    if (this->_cam_granted_flag)
//        this->on_app_init_window();
//}
//
//
//void AppEngine::enable_ui(void)
//{
//    JNIEnv *jni;
//
//    this->_app->activity->vm->AttachCurrentThread(&jni, NULL);
//    jclass clazz = jni->GetObjectClass(this->_app->activity->clazz);
//    jmethodID methodID = jni->GetMethodID(clazz, "EnableUI", "()V");
//    jni->CallVoidMethod(this->_app->activity->clazz, methodID);
//    this->_app->activity->vm->DetachCurrentThread();
//}
//
//void AppEngine::init_background(void)
//{
//    int32_t ret = -1;
//
//    void* data = nullptr;
//
//    AHardwareBuffer_Desc hb_desc;
//    hb_desc.width = this->_img_res.width * this->_img_res.height * 4;
//    hb_desc.height = 1;
//    hb_desc.format = AHARDWAREBUFFER_FORMAT_BLOB;
//    hb_desc.rfu0 = 0;
//    hb_desc.rfu1 = 0;
//    hb_desc.layers = 1;
//    hb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;
//    ret = AHardwareBuffer_allocate(&hb_desc, &this->_bk_hb);
//    if (ret < 0)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_allocate -> %d", ret);
//
//    ret = AHardwareBuffer_lock(this->_bk_hb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &data);
//    if (ret < 0)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_lock -> %d", ret);
//
//    memcpy(data, this->_bk_img, (this->_img_res.width * this->_img_res.height * 4));
//
//    ret = AHardwareBuffer_unlock(this->_bk_hb, NULL);
//    if (ret < 0)
//        __android_log_print(ANDROID_LOG_DEBUG, "CAM2NCNN2WIN", "AppEngine::init_background AHardwareBuffer_unlock -> %d", ret);
//
//}

AppEngine::~AppEngine(void)
{
    this->delete_camera();
    free(this->_bk_img);
    ncnn::destroy_gpu_instance();
}


/*----------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------native activity----------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
static AppEngine* p_engine = nullptr;


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

    JNIEXPORT void JNICALL Java_com_yyang_camncnnwin_MainActivity_openCamera(JNIEnv* env, jobject thiz, jint width, jint height)
    {
        p_engine->create_camera(width, height);
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


