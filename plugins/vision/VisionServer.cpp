#include "VisionServer.h"
#include "base/Logger.h"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

// JSON parsing (jsoncpp)
#include <json/json.h>

VisionServer::VisionServer(const std::string& ip, int port, 
                           IOMultiplexer::IOType io_type,
                           EnhancedConfigReader* config)
    : ApplicationServer(ip, port, io_type, nullptr)
    , config_(config)
    , running_(false)
    , is_recording_(false)
    , mqtt_client_(nullptr)
    , stats_{}
{
    Logger::info("VisionServer initializing");
}

VisionServer::~VisionServer() {
    stop();
}

bool VisionServer::start() {
    Logger::info("VisionServer starting");
    
    // Initialize MQTT
    if (!initMQTT()) {
        Logger::warn("MQTT init failed, will continue without event push");
    }
    
    // Initialize video source
    if (!initVideoSource()) {
        Logger::error("Video source init failed");
        return false;
    }
    
    // Initialize detection algorithm
    if (!initDetectionAlgorithm()) {
        Logger::error("Detection algorithm init failed");
        return false;
    }
    
    // Create output directories (创建输出目录：根目录、截图目录、录像目录)
    mkdir(output_dir_.c_str(), 0755);
    mkdir(snapshot_dir_.c_str(), 0755);
    mkdir(recording_dir_.c_str(), 0755);
    
    // Start TCP server
    if (!ApplicationServer::start()) {
        Logger::error("TCP server start failed");
        return false;
    }
    
    // Start video processing thread
    running_ = true;
    processing_thread_ = std::thread(&VisionServer::videoProcessingThread, this);
    
    Logger::info("VisionServer started on port " + std::to_string(m_port));
    Logger::info("Video source: " + source_path_);
    Logger::info("Detection algorithm: " + std::to_string(static_cast<int>(algorithm_)));
    Logger::info("Output directory: " + output_dir_);
    
    return true;
}

void VisionServer::stop() {
    Logger::info("VisionServer stopping");
    
    running_ = false;
    
    // Stop recording
    if (is_recording_) {
        stopRecording();
    }
    
    // Wait for processing thread
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    // Release video resources
#ifdef ENABLE_VISION_SERVER
    if (video_capture_.isOpened()) {
        video_capture_.release();
    }
#endif
    
    // Disconnect MQTT
#ifdef ENABLE_VISION_SERVER
    if (mqtt_client_ && mqtt_client_->is_connected()) {
        try {
            mqtt_client_->disconnect()->wait();
        } catch (const std::exception& e) {
            Logger::error("MQTT disconnect failed: " + std::string(e.what()));
        }
    }
#endif
    
    ApplicationServer::stop();
    Logger::info("VisionServer stopped");
}

void VisionServer::initializeProtocolRouter() {
    // VisionServer uses HTTP protocol mainly
    // HTTP requests are handled directly in onDataReceived
}

void VisionServer::onDataReceived(int clientFd, const char* data, size_t len) {
    std::string request(data, len);
    
    // Check if HTTP request
    if (request.find("GET ") == 0 || request.find("POST ") == 0 || 
        request.find("PUT ") == 0 || request.find("DELETE ") == 0) {
        std::string response = handleHttpRequest(request, clientFd);
        send(clientFd, response.c_str(), response.length(), 0);
    } else {
        // Not HTTP request, return error
        std::string error_response = "HTTP/1.1 400 Bad Request\r\n"
                                    "Content-Type: text/plain\r\n"
                                    "Content-Length: 11\r\n"
                                    "\r\n"
                                    "Bad Request";
        send(clientFd, error_response.c_str(), error_response.length(), 0);
    }
}

std::string VisionServer::handleHttpRequest(const std::string& request, int clientFd) {
    // Parse HTTP request line
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;
    
    Logger::info("HTTP request: " + method + " " + path);
    
    // Parse path and parameters
    std::string command;
    std::vector<std::string> args;
    
    if (!parseRequestPath(path, command, args)) {
        return generateJsonResponse(false, "", "Invalid request path");
    }
    
    // Handle business logic
    std::string result = handleBusinessLogic(command, args);
    
    // Generate HTTP response
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << result.length() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "\r\n";
    response << result;
    
    return response.str();
}

bool VisionServer::parseRequestPath(const std::string& path, 
                                    std::string& command, 
                                    std::vector<std::string>& args) {
    // Remove query parameters
    std::string clean_path = path;
    size_t query_pos = clean_path.find('?');
    if (query_pos != std::string::npos) {
        clean_path = clean_path.substr(0, query_pos);
    }
    
    // Split path
    std::istringstream iss(clean_path);
    std::string segment;
    std::vector<std::string> segments;
    
    while (std::getline(iss, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }
    
    if (segments.empty()) {
        return false;
    }
    
    command = segments[0];
    for (size_t i = 1; i < segments.size(); ++i) {
        args.push_back(segments[i]);
    }
    
    return true;
}

std::string VisionServer::handleBusinessLogic(const std::string& command, 
                                              const std::vector<std::string>& args) {
    try {
        if (command == "status") {
            return handleGetStatus();
        } 
        else if (command == "config") {
            if (args.empty()) {
                return handleGetConfig();
            } else {
                return handleSetConfig(args);
            }
        }
        else if (command == "snapshot") {
            return handleGetSnapshot();
        }
        else if (command == "recording") {
            if (args.empty()) {
                return generateJsonResponse(false, "", "Missing action parameter (start/stop)");
            }
            if (args[0] == "start") {
                return handleStartRecording();
            } else if (args[0] == "stop") {
                return handleStopRecording();
            } else {
                return generateJsonResponse(false, "", "Invalid action (start/stop)");
            }
        }
        else if (command == "events") {
            int limit = 10;
            if (!args.empty()) {
                limit = std::stoi(args[0]);
            }
            return handleGetEvents(limit);
        }
        else {
            return generateJsonResponse(false, "", "Unknown command: " + command);
        }
    } catch (const std::exception& e) {
        Logger::error("Business logic exception: " + std::string(e.what()));
        return generateJsonResponse(false, "", std::string("Exception: ") + e.what());
    }
}

void VisionServer::onClientConnected(int clientFd) {
    Logger::info("Client connected: fd=" + std::to_string(clientFd));
}

void VisionServer::onClientDisconnected(int clientFd) {
    Logger::info("Client disconnected: fd=" + std::to_string(clientFd));
}

// ==================== Initialization Functions ====================

bool VisionServer::initMQTT() {
#ifdef ENABLE_VISION_SERVER
    try {
        mqtt_broker_ = config_->getString("vision.mqtt.broker", "tcp://mosquitto:1883");
        mqtt_client_id_ = config_->getString("vision.mqtt.client_id", "vision_server");
        mqtt_topic_prefix_ = config_->getString("vision.mqtt.topic_prefix", "vision/events");
        mqtt_qos_ = std::stoi(config_->getString("vision.mqtt.qos", "1"));
        
        Logger::info("MQTT config: broker=" + mqtt_broker_ + ", client_id=" + mqtt_client_id_);
        
        mqtt_client_ = std::make_unique<mqtt::async_client>(mqtt_broker_, mqtt_client_id_);
        
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(20);
        conn_opts.set_clean_session(true);
        conn_opts.set_automatic_reconnect(true);
        
        auto tok = mqtt_client_->connect(conn_opts);
        tok->wait_for(std::chrono::seconds(5));
        
        if (mqtt_client_->is_connected()) {
            Logger::info("MQTT connected successfully");
            return true;
        } else {
            Logger::warn("MQTT connection timeout");
            return false;
        }
    } catch (const std::exception& e) {
        Logger::error("MQTT init exception: " + std::string(e.what()));
        return false;
    }
#else
    Logger::warn("MQTT not compiled (need ENABLE_VISION_SERVER macro)");
    return false;
#endif
}

bool VisionServer::initVideoSource() {
#ifdef ENABLE_VISION_SERVER
    try {
        // Read video source config
        std::string source_type_str = config_->getString("vision.source.type", "camera");
        source_path_ = config_->getString("vision.source.path", "0");
        frame_width_ = std::stoi(config_->getString("vision.source.width", "640"));
        frame_height_ = std::stoi(config_->getString("vision.source.height", "480"));
        target_fps_ = std::stoi(config_->getString("vision.source.fps", "30"));
        
        // Parse source type
        if (source_type_str == "test") {
            source_type_ = VideoSourceType::VIDEO_FILE;  // Use VIDEO_FILE type for test mode
            Logger::info("Test mode enabled - will generate synthetic video frames");
            return true;  // No need to open actual video source in test mode
        } else if (source_type_str == "camera") {
            source_type_ = VideoSourceType::CAMERA;
            // Camera ID (0 = default camera)
            int camera_id = std::stoi(source_path_);
            if (!video_capture_.open(camera_id)) {
                Logger::error("Cannot open camera: " + std::to_string(camera_id));
                return false;
            }
        } else if (source_type_str == "rtsp") {
            source_type_ = VideoSourceType::RTSP;
            if (!video_capture_.open(source_path_)) {
                Logger::error("Cannot open RTSP stream: " + source_path_);
                return false;
            }
        } else if (source_type_str == "file") {
            source_type_ = VideoSourceType::VIDEO_FILE;
            if (!video_capture_.open(source_path_)) {
                Logger::error("Cannot open video file: " + source_path_);
                return false;
            }
        } else {
            Logger::error("Unsupported video source type: " + source_type_str);
            return false;
        }
        
        // Set video parameters
        video_capture_.set(cv::CAP_PROP_FRAME_WIDTH, frame_width_);
        video_capture_.set(cv::CAP_PROP_FRAME_HEIGHT, frame_height_);
        video_capture_.set(cv::CAP_PROP_FPS, target_fps_);
        
        Logger::info("Video source initialized: type=" + source_type_str + ", path=" + source_path_ + 
                 ", size=" + std::to_string(frame_width_) + "x" + std::to_string(frame_height_) + 
                 ", fps=" + std::to_string(target_fps_));
        
        return true;
    } catch (const std::exception& e) {
        Logger::error("Video source init exception: " + std::string(e.what()));
        return false;
    }
#else
    Logger::error("OpenCV not compiled (need ENABLE_VISION_SERVER macro)");
    return false;
#endif
}

bool VisionServer::initDetectionAlgorithm() {
    try {
        // 使用正确的配置键名（detection.xxx 而不是 vision.detection.xxx）
        std::string algorithm_str = config_->getString("detection.algorithm", "motion");
        detection_threshold_ = config_->getDouble("detection.threshold", 0.3);
        min_contour_area_ = config_->getInt("detection.min_area", 500);
        
        // 事件节流配置（默认2000ms，即每2秒最多1个事件）
        event_cooldown_ms_ = config_->getInt("detection.event_cooldown_ms", 2000);
        
        // 录像和截图配置
        enable_recording_ = config_->getBool("recording.enabled", false);
        recording_duration_ = config_->getInt("recording.duration", 10);
        enable_snapshot_ = config_->getBool("snapshot.enabled", true);
        
        // 输出目录配置（支持图片和视频分开存储）
        output_dir_ = config_->getString("output.directory", "./output/vision");
        snapshot_dir_ = config_->getString("output.snapshot_dir", output_dir_ + "/snapshots");
        recording_dir_ = config_->getString("output.recording_dir", output_dir_ + "/recordings");
        
        Logger::info("📹 录像配置: enabled=" + std::string(enable_recording_ ? "true" : "false") + 
                    ", duration=" + std::to_string(recording_duration_) + "s");
        Logger::info("⏱️  事件节流: " + std::to_string(event_cooldown_ms_) + "ms 冷却时间");
        Logger::info("📁 输出目录: 截图=" + snapshot_dir_ + ", 录像=" + recording_dir_);
        
        // Parse algorithm type
        if (algorithm_str == "motion") {
            algorithm_ = DetectionAlgorithm::MOTION_DETECTION;
        } else if (algorithm_str == "contour") {
            algorithm_ = DetectionAlgorithm::CONTOUR_DETECTION;
        } else if (algorithm_str == "template") {
            algorithm_ = DetectionAlgorithm::TEMPLATE_MATCH;
        } else if (algorithm_str == "color") {
            algorithm_ = DetectionAlgorithm::COLOR_DETECTION;
        } else {
            Logger::warn("Unknown detection algorithm: " + algorithm_str + ", using default: motion");
            algorithm_ = DetectionAlgorithm::MOTION_DETECTION;
        }
        
        Logger::info("Detection algorithm config: algorithm=" + algorithm_str + 
                 ", threshold=" + std::to_string(detection_threshold_) + 
                 ", min_area=" + std::to_string(min_contour_area_));
        
        return true;
    } catch (const std::exception& e) {
        Logger::error("Detection algorithm init exception: " + std::string(e.what()));
        return false;
    }
}

// ==================== Video Processing Thread ====================

void VisionServer::videoProcessingThread() {
    Logger::info("Video processing thread started");
    
#ifdef ENABLE_VISION_SERVER
    cv::Mat current_frame;
    auto start_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    
    while (running_) {
        // Generate test frame or read from video source
        std::string source_type_str = config_->getString("vision.source.type", "camera");
        if (source_type_str == "test") {
            // Generate synthetic test frame
            current_frame = cv::Mat(frame_height_, frame_width_, CV_8UC3);
            // Create a gradient background
            for (int y = 0; y < frame_height_; y++) {
                for (int x = 0; x < frame_width_; x++) {
                    current_frame.at<cv::Vec3b>(y, x) = cv::Vec3b(
                        (x * 255) / frame_width_,
                        (y * 255) / frame_height_,
                        128
                    );
                }
            }
            // Add a moving circle (simulates motion)
            int cx = (frame_count * 5) % frame_width_;
            int cy = frame_height_ / 2;
            cv::circle(current_frame, cv::Point(cx, cy), 30, cv::Scalar(0, 0, 255), -1);
            
            // Add text overlay
            cv::putText(current_frame, "Test Frame " + std::to_string(frame_count), 
                       cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
        } else {
            // Read from actual video source
            if (!video_capture_.read(current_frame)) {
                Logger::warn("Failed to read video frame");
                if (source_type_ == VideoSourceType::VIDEO_FILE) {
                    // Video file ended, restart
                    video_capture_.set(cv::CAP_PROP_POS_FRAMES, 0);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }
        }
        
        if (current_frame.empty()) {
            continue;
        }
        
        auto frame_start = std::chrono::steady_clock::now();
        
        // Preprocess
        cv::Mat processed = preprocessFrame(current_frame);
        
        // Execute detection
        std::vector<DetectionEvent> events;
        switch (algorithm_) {
            case DetectionAlgorithm::MOTION_DETECTION:
                if (!previous_frame_.empty()) {
                    events = detectMotion(processed, previous_frame_);
                }
                break;
            case DetectionAlgorithm::CONTOUR_DETECTION:
                events = detectContours(processed);
                break;
            case DetectionAlgorithm::TEMPLATE_MATCH:
                events = detectByTemplate(processed);
                break;
            case DetectionAlgorithm::COLOR_DETECTION:
                events = detectByColor(processed);
                break;
        }
        
        // Handle detected events (使用事件节流)
        if (!events.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_event_time_).count();
            
            if (elapsed >= event_cooldown_ms_) {
                // 只处理第一个事件（最显著的）
                handleDetectionEvent(events[0]);
                last_event_time_ = now;
            } else {
                // 在冷却期内，忽略事件
                Logger::debug("事件被节流忽略（冷却中）");
            }
        }
        
        // Save current frame for next comparison
        processed.copyTo(previous_frame_);
        
        // Save latest frame (for HTTP snapshot)
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            current_frame.copyTo(latest_frame_);
        }
        
        // Recording
        if (is_recording_ && video_writer_.isOpened()) {
            video_writer_.write(current_frame);
            
            // Check if auto-recording duration exceeded
            if (enable_recording_) {
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - recording_start_time_).count();
                if (duration >= recording_duration_) {
                    std::lock_guard<std::mutex> lock(recording_mutex_);
                    Logger::info("Auto-recording duration reached, stopping recording");
                    stopRecording();
                }
            }
        }
        
        // Update statistics
        frame_count++;
        auto frame_end = std::chrono::steady_clock::now();
        double process_time = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_processed = frame_count;
            stats_.avg_process_time_ms = (stats_.avg_process_time_ms * (frame_count - 1) + process_time) / frame_count;
            stats_.is_recording = is_recording_;
        }
        
        // Calculate FPS (update every second)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - start_time).count();
        if (elapsed >= 1.0) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.current_fps = frame_count / elapsed;
            frame_count = 0;
            start_time = now;
        }
        
        // Control frame rate
        int target_delay = 1000 / target_fps_;
        int actual_delay = std::max(1, target_delay - static_cast<int>(process_time));
        std::this_thread::sleep_for(std::chrono::milliseconds(actual_delay));
    }
#endif
    
    Logger::info("Video processing thread ended");
}

// ==================== Image Processing Functions ====================

#ifdef ENABLE_VISION_SERVER

cv::Mat VisionServer::preprocessFrame(const cv::Mat& frame) {
    cv::Mat gray, blurred;
    
    // Convert to grayscale
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    
    // Gaussian blur for noise reduction
    cv::GaussianBlur(gray, blurred, cv::Size(21, 21), 0);
    
    return blurred;
}

std::vector<DetectionEvent> VisionServer::detectMotion(const cv::Mat& current, const cv::Mat& previous) {
    std::vector<DetectionEvent> events;
    
    // Frame difference
    cv::Mat diff, thresh;
    cv::absdiff(previous, current, diff);
    cv::threshold(diff, thresh, 25, 255, cv::THRESH_BINARY);
    
    // Dilate to fill holes
    cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1, -1), 2);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < min_contour_area_) {
            continue;
        }
        
        // Create detection event
        cv::Rect bbox = cv::boundingRect(contour);
        
        DetectionEvent event;
        event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        event.event_type = "motion";
        event.description = "Motion detected";
        event.confidence = std::min(1.0, area / (min_contour_area_ * 10.0));
        event.x = bbox.x;
        event.y = bbox.y;
        event.width = bbox.width;
        event.height = bbox.height;
        event.image_path = "";
        
        events.push_back(event);
    }
    
    return events;
}

std::vector<DetectionEvent> VisionServer::detectContours(const cv::Mat& frame) {
    std::vector<DetectionEvent> events;
    
    // Edge detection
    cv::Mat edges;
    cv::Canny(frame, edges, 50, 150);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < min_contour_area_) {
            continue;
        }
        
        cv::Rect bbox = cv::boundingRect(contour);
        
        DetectionEvent event;
        event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        event.event_type = "contour";
        event.description = "Contour detected";
        event.confidence = 0.8;
        event.x = bbox.x;
        event.y = bbox.y;
        event.width = bbox.width;
        event.height = bbox.height;
        event.image_path = "";
        
        events.push_back(event);
    }
    
    return events;
}

std::vector<DetectionEvent> VisionServer::detectByTemplate(const cv::Mat& frame) {
    // TODO: Implement template matching
    // Need to preload template images
    return {};
}

std::vector<DetectionEvent> VisionServer::detectByColor(const cv::Mat& frame) {
    // TODO: Implement color detection
    // Example: detect objects in specific color range
    return {};
}

#endif

// ==================== Event Handling ====================

void VisionServer::handleDetectionEvent(const DetectionEvent& event) {
#ifdef ENABLE_VISION_SERVER
    Logger::info("Event detected: type=" + event.event_type + 
             ", confidence=" + std::to_string(event.confidence) + 
             ", pos=(" + std::to_string(event.x) + "," + std::to_string(event.y) + ")" +
             " size=" + std::to_string(event.width) + "x" + std::to_string(event.height));
#endif
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.events_detected++;
        stats_.last_event_time = event.timestamp;
    }
    
    // Save to event queue
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        event_queue_.push(event);
        recent_events_.push_back(event);
        
        // Keep recent events limit
        if (recent_events_.size() > MAX_RECENT_EVENTS) {
            recent_events_.erase(recent_events_.begin());
        }
    }
    
    // Save snapshot
    if (enable_snapshot_) {
        saveEventSnapshot(event);
    }
    
    // Auto recording: 检测到事件时自动开始录像
    if (enable_recording_ && !is_recording_) {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        if (!is_recording_) {  // Double-check
            Logger::info("Auto-recording triggered by event detection");
            startRecording();
            recording_start_time_ = std::chrono::steady_clock::now();
        }
    }
    
    // Publish to MQTT
    publishEventToMQTT(event);
}

void VisionServer::publishEventToMQTT(const DetectionEvent& event) {
#ifdef ENABLE_VISION_SERVER
    if (!mqtt_client_ || !mqtt_client_->is_connected()) {
        return;
    }
    
    try {
        // Build JSON message
        Json::Value root;
        root["timestamp"] = static_cast<Json::Int64>(event.timestamp);
        root["event_type"] = event.event_type;
        root["description"] = event.description;
        root["confidence"] = event.confidence;
        root["x"] = event.x;
        root["y"] = event.y;
        root["width"] = event.width;
        root["height"] = event.height;
        root["image_path"] = event.image_path;
        
        Json::StreamWriterBuilder writer;
        std::string json_str = Json::writeString(writer, root);
        
        // Publish to MQTT
        std::string topic = mqtt_topic_prefix_ + "/" + event.event_type;
        auto msg = mqtt::make_message(topic, json_str);
        msg->set_qos(mqtt_qos_);
        mqtt_client_->publish(msg);
        
        Logger::debug("Event published to MQTT: topic=" + topic);
    } catch (const std::exception& e) {
        Logger::error("MQTT publish failed: " + std::string(e.what()));
    }
#endif
}

void VisionServer::saveEventSnapshot(const DetectionEvent& event) {
#ifdef ENABLE_VISION_SERVER
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (latest_frame_.empty()) {
        return;
    }
    
    try {
        std::string timestamp = getTimestamp();
        std::string filename = snapshot_dir_ + "/event_" + event.event_type + "_" + timestamp + ".jpg";
        
        // Draw detection box on image
        cv::Mat annotated = latest_frame_.clone();
        cv::rectangle(annotated, 
                     cv::Rect(event.x, event.y, event.width, event.height),
                     cv::Scalar(0, 255, 0), 2);
        
        // Add text label
        std::string label = event.event_type + " " + std::to_string(static_cast<int>(event.confidence * 100)) + "%";
        cv::putText(annotated, label, cv::Point(event.x, event.y - 10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
        
        cv::imwrite(filename, annotated);
        Logger::debug("Event snapshot saved: " + filename);
    } catch (const std::exception& e) {
        Logger::error("Save snapshot failed: " + std::string(e.what()));
    }
#endif
}

// ==================== HTTP API Handlers ====================

std::string VisionServer::handleGetStatus() {
    Json::Value data;
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        data["frames_processed"] = static_cast<Json::Int64>(stats_.frames_processed);
        data["events_detected"] = static_cast<Json::Int64>(stats_.events_detected);
        data["current_fps"] = stats_.current_fps;
        data["avg_process_time_ms"] = stats_.avg_process_time_ms;
        data["last_event_time"] = static_cast<Json::Int64>(stats_.last_event_time);
        data["is_recording"] = stats_.is_recording;
    }
    
    data["source_type"] = static_cast<int>(source_type_);
    data["source_path"] = source_path_;
    data["algorithm"] = static_cast<int>(algorithm_);
    data["detection_threshold"] = detection_threshold_;
    
    Json::StreamWriterBuilder writer;
    std::string data_str = Json::writeString(writer, data);
    
    return generateJsonResponse(true, data_str, "Status query successful");
}

std::string VisionServer::handleGetConfig() {
    Json::Value data;
    data["frame_width"] = frame_width_;
    data["frame_height"] = frame_height_;
    data["target_fps"] = target_fps_;
    data["detection_threshold"] = detection_threshold_;
    data["min_contour_area"] = min_contour_area_;
    data["enable_recording"] = enable_recording_;
    data["enable_snapshot"] = enable_snapshot_;
    data["output_dir"] = output_dir_;
    
    Json::StreamWriterBuilder writer;
    std::string data_str = Json::writeString(writer, data);
    
    return generateJsonResponse(true, data_str, "Config query successful");
}

std::string VisionServer::handleSetConfig(const std::vector<std::string>& args) {
    // TODO: Implement config update
    return generateJsonResponse(false, "", "Config update not implemented yet");
}

std::string VisionServer::handleGetSnapshot() {
#ifdef ENABLE_VISION_SERVER
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (latest_frame_.empty()) {
        return generateJsonResponse(false, "", "No frame available");
    }
    
    try {
        // Encode as JPEG
        std::vector<uchar> buffer;
        cv::imencode(".jpg", latest_frame_, buffer);
        
        // Base64 encode (simplified, should use proper base64 library)
        std::string base64_data(buffer.begin(), buffer.end());
        
        Json::Value data;
        data["image"] = base64_data;
        data["width"] = latest_frame_.cols;
        data["height"] = latest_frame_.rows;
        data["timestamp"] = getTimestamp();
        
        Json::StreamWriterBuilder writer;
        std::string data_str = Json::writeString(writer, data);
        
        return generateJsonResponse(true, data_str, "Snapshot successful");
    } catch (const std::exception& e) {
        return generateJsonResponse(false, "", std::string("Snapshot failed: ") + e.what());
    }
#else
    return generateJsonResponse(false, "", "OpenCV not compiled");
#endif
}

std::string VisionServer::handleStartRecording() {
    if (is_recording_) {
        return generateJsonResponse(false, "", "Already recording");
    }
    
    startRecording();
    return generateJsonResponse(true, current_video_file_, "Recording started");
}

std::string VisionServer::handleStopRecording() {
    if (!is_recording_) {
        return generateJsonResponse(false, "", "Not currently recording");
    }
    
    stopRecording();
    return generateJsonResponse(true, "", "Recording stopped");
}

std::string VisionServer::handleGetEvents(int limit) {
    Json::Value data(Json::arrayValue);
    
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        int count = 0;
        for (auto it = recent_events_.rbegin(); it != recent_events_.rend() && count < limit; ++it, ++count) {
            Json::Value event_json;
            event_json["timestamp"] = static_cast<Json::Int64>(it->timestamp);
            event_json["event_type"] = it->event_type;
            event_json["description"] = it->description;
            event_json["confidence"] = it->confidence;
            event_json["x"] = it->x;
            event_json["y"] = it->y;
            event_json["width"] = it->width;
            event_json["height"] = it->height;
            event_json["image_path"] = it->image_path;
            
            data.append(event_json);
        }
    }
    
    Json::StreamWriterBuilder writer;
    std::string data_str = Json::writeString(writer, data);
    
    return generateJsonResponse(true, data_str, "Events query successful");
}

// ==================== Recording Functions ====================

void VisionServer::startRecording() {
#ifdef ENABLE_VISION_SERVER
    std::string timestamp = getTimestamp();
    current_video_file_ = recording_dir_ + "/recording_" + timestamp + ".avi";
    
    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    video_writer_.open(current_video_file_, fourcc, target_fps_, 
                      cv::Size(frame_width_, frame_height_));
    
    if (video_writer_.isOpened()) {
        is_recording_ = true;
        recording_start_time_ = std::chrono::steady_clock::now();
        Logger::info("Recording started: " + current_video_file_);
    } else {
        Logger::error("Recording file creation failed: " + current_video_file_);
    }
#endif
}

void VisionServer::stopRecording() {
#ifdef ENABLE_VISION_SERVER
    is_recording_ = false;
    if (video_writer_.isOpened()) {
        video_writer_.release();
        Logger::info("Recording stopped: " + current_video_file_);
    }
#endif
}

// ==================== Utility Functions ====================

std::string VisionServer::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

std::string VisionServer::saveFrameToFile(const std::string& frame_data, const std::string& prefix) {
    std::string timestamp = getTimestamp();
    std::string filename = output_dir_ + "/" + prefix + "_" + timestamp + ".jpg";
    
    std::ofstream file(filename, std::ios::binary);
    file.write(frame_data.c_str(), frame_data.length());
    file.close();
    
    return filename;
}

