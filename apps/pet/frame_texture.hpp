#pragma once

#include <eui/image.h>
#include <glad/glad.h>
#include <memory>
#include <span>

namespace mirage::pet_ui {
// UI resource adapter for the product's OpenGL backend. Only the UI owner
// calls upload/reset, in EUI's active window scope. EUI retains the native
// resource through its public lifetime contract until its last GPU use.
class FrameTexture {
  public:
    bool upload(std::span<const std::uint8_t> rgba) {
        if (rgba.size() != 512u * 512u * 4u)
            return false;
        const auto device = eui::image::gpuDevice();
        if (device.api != eui::GpuApi::OpenGL || !device.identity)
            return false;
        if (resource_ && resource_->device != device.identity)
            return false;

        auto resource = resource_;
        if (!resource)
            resource = std::make_shared<Resource>();
        resource->device = device.identity;
        GLint binding = 0, buffer = 0, alignment = 0, row_length = 0, skip_x = 0, skip_y = 0,
              swap = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_x);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_y);
        glGetIntegerv(GL_UNPACK_SWAP_BYTES, &swap);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
        const bool create = !resource->texture;
        if (create)
            glGenTextures(1, &resource->texture);
        glBindTexture(GL_TEXTURE_2D, resource->texture);
        if (create) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         rgba.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE,
                            rgba.data());
        }
        const auto error = glGetError();
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(binding));
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_x);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_y);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, swap);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
        if (error != GL_NO_ERROR || !resource->texture)
            return false;
        if (!image_) {
            eui::GpuImageDescriptor descriptor;
            descriptor.device = device;
            descriptor.width = descriptor.height = 512;
            descriptor.texture = resource->texture;
            image_ = eui::image::importGpuImage(descriptor, resource);
            if (!image_)
                return false;
            resource_ = std::move(resource);
        }
        ++revision_;
        return true;
    }
    const std::shared_ptr<const eui::GpuImage> &image() const { return image_; }
    std::uint64_t revision() const { return revision_; }
    // Stop displaying a failed publication, retaining this window's bounded
    // resource for a later status-driven recovery. No active binding remains.
    void hide() { image_.reset(); }
    void reset() {
        image_.reset();
        resource_.reset();
    }

    // Only at final application shutdown, after producers stop and before
    // EUI/device teardown. Its UI tree can retain descriptors until destruction;
    // no subsequent paint is allowed. GL retires outstanding uses safely.
    void shutdown() {
        if (resource_)
            resource_->release();
        reset();
    }

  private:
    struct Resource {
        GLuint texture = 0;
        std::uint64_t device = 0;
        void release() {
            if (texture) {
                glDeleteTextures(1, &texture);
                texture = 0;
            }
        }
        ~Resource() { release(); }
    };
    std::shared_ptr<Resource> resource_;
    std::shared_ptr<const eui::GpuImage> image_;
    std::uint64_t revision_ = 0;
};
} // namespace mirage::pet_ui
