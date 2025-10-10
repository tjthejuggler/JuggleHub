#include "GpuHsvConverter.hpp"
#include <iostream>

GpuHsvConverter::GpuHsvConverter() : gpu_enabled_(false) {
    // Check if OpenCL is available
    if (cv::ocl::haveOpenCL()) {
        // Try to initialize OpenCL
        cv::ocl::Context context;
        if (!context.create(cv::ocl::Device::TYPE_GPU)) {
            std::cerr << "[GpuHsvConverter] Failed to create OpenCL context for GPU" << std::endl;
            std::cerr << "[GpuHsvConverter] Falling back to CPU for HSV conversion" << std::endl;
            cv::ocl::setUseOpenCL(false);
            gpu_enabled_ = false;
        } else {
            // Successfully initialized OpenCL
            cv::ocl::Device device = cv::ocl::Device::getDefault();
            std::cout << "[GpuHsvConverter] GPU acceleration ENABLED" << std::endl;
            std::cout << "[GpuHsvConverter] Device: " << device.name() << std::endl;
            std::cout << "[GpuHsvConverter] Vendor: " << device.vendorName() << std::endl;
            std::cout << "[GpuHsvConverter] OpenCL Version: " << device.OpenCL_C_Version() << std::endl;
            
            cv::ocl::setUseOpenCL(true);
            gpu_enabled_ = true;
        }
    } else {
        std::cerr << "[GpuHsvConverter] OpenCL not available on this system" << std::endl;
        std::cerr << "[GpuHsvConverter] Falling back to CPU for HSV conversion" << std::endl;
        cv::ocl::setUseOpenCL(false);
        gpu_enabled_ = false;
    }
}

cv::Mat GpuHsvConverter::convertRoiToHsv(const cv::Mat& bgr_frame, const cv::Rect& roi) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!gpu_enabled_) {
        // Fallback to CPU conversion
        debug_counters_.hsv_roi_cpu_fallback++;
        
        cv::Mat bgr_roi = bgr_frame(roi);
        cv::Mat hsv_roi;
        cv::cvtColor(bgr_roi, hsv_roi, cv::COLOR_BGR2HSV);
        return hsv_roi;
    }
    
    try {
        // Extract ROI from CPU memory
        cv::Mat bgr_roi = bgr_frame(roi);
        
        // Upload to GPU memory (UMat)
        cv::UMat gpu_bgr_roi;
        bgr_roi.copyTo(gpu_bgr_roi);
        
        // Perform conversion on GPU
        cv::UMat gpu_hsv_roi;
        cv::cvtColor(gpu_bgr_roi, gpu_hsv_roi, cv::COLOR_BGR2HSV);
        
        // Download result back to CPU memory
        cv::Mat hsv_roi;
        gpu_hsv_roi.copyTo(hsv_roi);
        
        debug_counters_.hsv_roi_gpu_success++;
        
        return hsv_roi;
        
    } catch (const cv::Exception& e) {
        std::cerr << "[GpuHsvConverter] GPU conversion failed: " << e.what() << std::endl;
        std::cerr << "[GpuHsvConverter] Falling back to CPU" << std::endl;
        
        debug_counters_.hsv_roi_cpu_fallback++;
        
        // Fallback to CPU
        cv::Mat bgr_roi = bgr_frame(roi);
        cv::Mat hsv_roi;
        cv::cvtColor(bgr_roi, hsv_roi, cv::COLOR_BGR2HSV);
        return hsv_roi;
    }
}

cv::Mat GpuHsvConverter::convertToHsv(const cv::Mat& bgr_frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!gpu_enabled_) {
        // Fallback to CPU conversion
        debug_counters_.hsv_full_cpu_fallback++;
        
        cv::Mat hsv_frame;
        cv::cvtColor(bgr_frame, hsv_frame, cv::COLOR_BGR2HSV);
        return hsv_frame;
    }
    
    try {
        // Upload to GPU memory (UMat)
        cv::UMat gpu_bgr;
        bgr_frame.copyTo(gpu_bgr);
        
        // Perform conversion on GPU
        cv::UMat gpu_hsv;
        cv::cvtColor(gpu_bgr, gpu_hsv, cv::COLOR_BGR2HSV);
        
        // Download result back to CPU memory
        cv::Mat hsv_frame;
        gpu_hsv.copyTo(hsv_frame);
        
        debug_counters_.hsv_full_gpu_success++;
        
        return hsv_frame;
        
    } catch (const cv::Exception& e) {
        std::cerr << "[GpuHsvConverter] GPU conversion failed: " << e.what() << std::endl;
        std::cerr << "[GpuHsvConverter] Falling back to CPU" << std::endl;
        
        debug_counters_.hsv_full_cpu_fallback++;
        
        // Fallback to CPU
        cv::Mat hsv_frame;
        cv::cvtColor(bgr_frame, hsv_frame, cv::COLOR_BGR2HSV);
        return hsv_frame;
    }
}

cv::Point2f GpuHsvConverter::findColorBlob(const cv::Mat& bgr_frame,
                                          const cv::Rect& roi,
                                          const cv::Scalar& min_hsv,
                                          const cv::Scalar& max_hsv,
                                          const cv::Scalar& min_hsv2,
                                          const cv::Scalar& max_hsv2,
                                          int roi_offset_x,
                                          int roi_offset_y) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!gpu_enabled_) {
        // Fallback to CPU implementation
        debug_counters_.blob_search_cpu_fallback++;
        cv::Mat bgr_roi = bgr_frame(roi);
        cv::Mat hsv_roi;
        cv::cvtColor(bgr_roi, hsv_roi, cv::COLOR_BGR2HSV);
        
        // Create mask for color
        cv::Mat mask1, mask2, mask;
        cv::inRange(hsv_roi, min_hsv, max_hsv, mask1);
        
        if (min_hsv2[0] >= 0) {
            cv::inRange(hsv_roi, min_hsv2, max_hsv2, mask2);
            cv::bitwise_or(mask1, mask2, mask);
        } else {
            mask = mask1;
        }
        
        // Find contours
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        // Find largest contour
        double max_area = 0;
        cv::Point2f best_center(-1, -1);
        
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area < 50.0) continue;
            
            cv::Moments m = cv::moments(contour);
            if (m.m00 == 0) continue;
            
            cv::Point2f center(m.m10 / m.m00 + roi_offset_x, m.m01 / m.m00 + roi_offset_y);
            
            if (area > max_area) {
                max_area = area;
                best_center = center;
            }
        }
        
        return best_center;
    }
    
    try {
        // Extract ROI and upload to GPU
        cv::Mat bgr_roi = bgr_frame(roi);
        cv::UMat gpu_bgr_roi;
        bgr_roi.copyTo(gpu_bgr_roi);
        
        // Convert to HSV on GPU
        cv::UMat gpu_hsv_roi;
        cv::cvtColor(gpu_bgr_roi, gpu_hsv_roi, cv::COLOR_BGR2HSV);
        
        // Create mask on GPU
        cv::UMat gpu_mask1, gpu_mask2, gpu_mask;
        cv::inRange(gpu_hsv_roi, min_hsv, max_hsv, gpu_mask1);
        
        if (min_hsv2[0] >= 0) {
            cv::inRange(gpu_hsv_roi, min_hsv2, max_hsv2, gpu_mask2);
            cv::bitwise_or(gpu_mask1, gpu_mask2, gpu_mask);
        } else {
            gpu_mask = gpu_mask1;
        }
        
        // Download mask to CPU for contour detection
        // Note: findContours is not available in OpenCV's GPU module
        cv::Mat mask;
        gpu_mask.copyTo(mask);
        
        // Find contours on CPU (this part cannot be GPU-accelerated in OpenCV)
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        // Find largest contour
        double max_area = 0;
        cv::Point2f best_center(-1, -1);
        
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area < 50.0) continue;
            
            cv::Moments m = cv::moments(contour);
            if (m.m00 == 0) continue;
            
            cv::Point2f center(m.m10 / m.m00 + roi_offset_x, m.m01 / m.m00 + roi_offset_y);
            
            if (area > max_area) {
                max_area = area;
                best_center = center;
            }
        }
        
        debug_counters_.blob_search_gpu_success++;
        
        return best_center;
        
    } catch (const cv::Exception& e) {
        std::cerr << "[GpuHsvConverter] GPU blob search failed: " << e.what() << std::endl;
        std::cerr << "[GpuHsvConverter] Falling back to CPU" << std::endl;
        
        debug_counters_.blob_search_cpu_fallback++;
        
        // Fallback to CPU
        cv::Mat bgr_roi = bgr_frame(roi);
        cv::Mat hsv_roi;
        cv::cvtColor(bgr_roi, hsv_roi, cv::COLOR_BGR2HSV);
        
        cv::Mat mask1, mask2, mask;
        cv::inRange(hsv_roi, min_hsv, max_hsv, mask1);
        
        if (min_hsv2[0] >= 0) {
            cv::inRange(hsv_roi, min_hsv2, max_hsv2, mask2);
            cv::bitwise_or(mask1, mask2, mask);
        } else {
            mask = mask1;
        }
        
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        double max_area = 0;
        cv::Point2f best_center(-1, -1);
        
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area < 50.0) continue;
            
            cv::Moments m = cv::moments(contour);
            if (m.m00 == 0) continue;
            
            cv::Point2f center(m.m10 / m.m00 + roi_offset_x, m.m01 / m.m00 + roi_offset_y);
            
            if (area > max_area) {
                max_area = area;
                best_center = center;
            }
        }
        
        return best_center;
    }
}

std::string GpuHsvConverter::getGpuInfo() const {
    if (!gpu_enabled_) {
        return "GPU acceleration disabled (using CPU)";
    }
    
    if (!cv::ocl::haveOpenCL()) {
        return "OpenCL not available";
    }
    
    cv::ocl::Device device = cv::ocl::Device::getDefault();
    std::string info = "GPU: " + device.name() +
                      " | Vendor: " + device.vendorName() +
                      " | OpenCL: " + device.OpenCL_C_Version();
    return info;
}