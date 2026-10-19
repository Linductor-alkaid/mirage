#include "../../apps/pet/frame_texture.hpp"
#include "../support/ipc_io.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <GLFW/glfw3.h>
#include <core/dsl_runtime.h>

int main() {
    mirage::testing::XvfbDisplay display(mirage::testing::find_xvfb(), "640x480x24");
    ::setenv("DISPLAY", display.display_name().c_str(), 1);
    ::unsetenv("WAYLAND_DISPLAY");
    MIRAGE_CHECK(glfwInit());
    core::window::WindowCreateRequest request;
    request.width = 264;
    request.height = 322;
    auto handle = core::window::createWindow(request);
    MIRAGE_CHECK(handle);
    if (!handle)
        return mirage::testing::finish("pet_texture_test");
    glfwMakeContextCurrent(static_cast<GLFWwindow *>(handle));
    core::render::initializeRenderBackendLoader();
    auto backend = core::render::createRenderBackend(handle);
    MIRAGE_CHECK(backend && backend->initialize());
    {
        core::render::ScopedRenderBackend scope(*backend);
        mirage::pet_ui::FrameTexture texture;
        MIRAGE_CHECK(!texture.upload({}));
        std::vector<std::uint8_t> rgba(512u * 512u * 4u);
        for (std::size_t i = 0; i < rgba.size(); ++i)
            rgba[i] = static_cast<std::uint8_t>(i * 13 + i / 512);
        // Hostile unpack state must neither reinterpret the pointer nor leak
        // adapter state back into EUI's next upload.
        GLuint pbo = 0;
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 16, nullptr, GL_STREAM_DRAW);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 640);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
        MIRAGE_CHECK(texture.upload(rgba));
        auto borrowed = texture.image();
        MIRAGE_CHECK(borrowed && texture.revision() == 1);
        const auto id = borrowed->descriptor().texture;
        for (const auto &[name, expected] :
             {std::pair{GL_UNPACK_ALIGNMENT, 8},
              {GL_UNPACK_ROW_LENGTH, 640},
              {GL_UNPACK_SKIP_PIXELS, 3},
              {GL_UNPACK_SKIP_ROWS, 2},
              {GL_UNPACK_SWAP_BYTES, 1},
              {GL_PIXEL_UNPACK_BUFFER_BINDING, static_cast<int>(pbo)}}) {
            GLint actual = 0;
            glGetIntegerv(static_cast<GLenum>(name), &actual);
            MIRAGE_CHECK(actual == expected);
        }
        rgba[3] = 197;
        MIRAGE_CHECK(texture.upload(rgba));
        MIRAGE_CHECK(texture.image()->descriptor().texture == id && texture.revision() == 2);
        glBindTexture(GL_TEXTURE_2D, id);
        std::vector<std::uint8_t> restored(rgba.size());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, restored.data());
        MIRAGE_CHECK(restored == rgba && glGetError() == GL_NO_ERROR);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glDeleteBuffers(1, &pbo);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);

        texture.reset();
        MIRAGE_CHECK(glIsTexture(id)); // a consumer still owns the resource
        borrowed.reset();
        MIRAGE_CHECK(!glIsTexture(id));
        MIRAGE_CHECK(texture.upload(rgba));
        const auto active_id = texture.image()->descriptor().texture;
        {
            core::dsl::Runtime runtime;
            MIRAGE_CHECK(runtime.initialize(handle));
            runtime.compose("pet", 264, 322, [&](auto &ui, const auto &) {
                ui.image("frame")
                    .size(240, 240)
                    .texture(texture.image(), texture.revision())
                    .build();
            });
            runtime.update(handle, 0, 1, 1);
            runtime.render(264, 322, 1, {0, 0, 0, 0});
            runtime.update(handle, 0, 1, 1);
            MIRAGE_CHECK(!runtime.isAnimating());
            std::uint8_t visible[4]{};
            glReadPixels(100, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, visible);
            MIRAGE_CHECK(visible[0] || visible[1] || visible[2]);
            // A failed publication can clear the UI binding without depending
            // on a future animation tick or destroying a still-borrowed texture.
            texture.hide();
            MIRAGE_CHECK(!texture.image() && glIsTexture(active_id));
            runtime.compose("pet", 264, 322, [&](auto &ui, const auto &) {
                ui.image("frame")
                    .size(240, 240)
                    .texture(texture.image(), texture.revision())
                    .build();
            });
            runtime.update(handle, 0, 1, 1);
            runtime.render(264, 322, 1, {0, 0, 0, 0});
            MIRAGE_CHECK(!runtime.isAnimating());
            std::uint8_t cleared[4]{};
            glReadPixels(100, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cleared);
            MIRAGE_CHECK(!cleared[0] && !cleared[1] && !cleared[2]);
            MIRAGE_CHECK(texture.upload(rgba));
            MIRAGE_CHECK(texture.image()->descriptor().texture == active_id);
            // Device teardown cannot wait for EUI's retained UI descriptors.
            auto last = texture.image();
            texture.shutdown();
            MIRAGE_CHECK(!glIsTexture(active_id));
            last.reset();
            runtime.shutdown();
        }
    }
    backend.reset();
    core::window::destroyWindow(handle);
    glfwTerminate();
    return mirage::testing::finish("pet_texture_test");
}
