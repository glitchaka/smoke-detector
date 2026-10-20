#pragma once

#include <algorithm>

namespace promptemon::viewer {

class ModelViewerState {
public:
    void beginDrag(float x, float y) noexcept {
        dragging_ = true;
        lastX_ = x;
        lastY_ = y;
    }

    void dragTo(float x, float y) noexcept {
        if (!dragging_) {
            return;
        }

        const float dx = x - lastX_;
        const float dy = y - lastY_;
        lastX_ = x;
        lastY_ = y;

        yawDegrees_ += dx * rotationSensitivity_;
        pitchDegrees_ = std::clamp(
            pitchDegrees_ + dy * rotationSensitivity_,
            minPitchDegrees_,
            maxPitchDegrees_);
    }

    void endDrag() noexcept { dragging_ = false; }

    void addZoom(float delta) noexcept {
        distance_ = std::clamp(distance_ - delta * zoomSensitivity_, minDistance_, maxDistance_);
    }

    float yawDegrees() const noexcept { return yawDegrees_; }
    float pitchDegrees() const noexcept { return pitchDegrees_; }
    float distance() const noexcept { return distance_; }

    void resetView() noexcept {
        yawDegrees_ = 0.0f;
        pitchDegrees_ = 0.0f;
        distance_ = 4.0f;
        dragging_ = false;
    }

private:
    bool dragging_ = false;
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    float yawDegrees_ = 0.0f;
    float pitchDegrees_ = 0.0f;
    float distance_ = 4.0f;

    float rotationSensitivity_ = 0.35f;
    float zoomSensitivity_ = 0.35f;
    float minPitchDegrees_ = -35.0f;
    float maxPitchDegrees_ = 35.0f;
    float minDistance_ = 1.5f;
    float maxDistance_ = 10.0f;
};

} // namespace promptemon::viewer
