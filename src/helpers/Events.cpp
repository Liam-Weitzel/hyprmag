#include "Events.hpp"

#include "../trackpad-color-picker.hpp"
#include "Clipboard.hpp"

void Events::geometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t width_mm, int32_t height_mm, int32_t subpixel, const char* make, const char* model,
                      int32_t transform) {
    const auto PMONITOR = (SMonitor*)data;

    PMONITOR->transform = (wl_output_transform)transform;
}

void Events::mode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    // ignored
}

void Events::done(void* data, wl_output* wl_output) {
    const auto PMONITOR = (SMonitor*)data;

    PMONITOR->ready = true;
}

void Events::scale(void* data, wl_output* wl_output, int32_t scale) {
    const auto PMONITOR = (SMonitor*)data;

    PMONITOR->scale = scale;
}

void Events::handleXDGOutputLogicalSize(void* data, struct zxdg_output_v1* output, int32_t width, int32_t height) {
    const auto PMONITOR = (SMonitor*)data;
    
    // The logical size compared to the physical size gives us the actual scale
    if (PMONITOR->size.x > 0 && width > 0) {
        PMONITOR->scale = (float)PMONITOR->size.x / (float)width;
    }
}

void Events::handleXDGOutputLogicalPosition(void* data, struct zxdg_output_v1* output, int32_t x, int32_t y) {}
void Events::handleXDGOutputDone(void* data, struct zxdg_output_v1* output) {}
void Events::handleXDGOutputName(void* data, struct zxdg_output_v1* output, const char* name) {}
void Events::handleXDGOutputDescription(void* data, struct zxdg_output_v1* output, const char* description) {}

void Events::name(void* data, wl_output* wl_output, const char* name) {
    const auto PMONITOR = (SMonitor*)data;

    if (name)
        PMONITOR->name = name;
}

void Events::description(void* data, wl_output* wl_output, const char* description) {
    // i do not care
}

void Events::ls_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t width, uint32_t height) {
    const auto PLAYERSURFACE = (CLayerSurface*)data;

    PLAYERSURFACE->m_pMonitor->size = Vector2D(width, height);

    PLAYERSURFACE->ACKSerial = serial;
    PLAYERSURFACE->wantsACK  = true;
    PLAYERSURFACE->working   = true;

    g_pTrackpadColorPicker->recheckACK();
}

void Events::handleGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        g_pTrackpadColorPicker->m_pXDGOutputMgr = (zxdg_output_manager_v1*)wl_registry_bind(
            registry, name, &zxdg_output_manager_v1_interface, 
            version > 2 ? 2 : version);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        g_pTrackpadColorPicker->m_pCompositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g_pTrackpadColorPicker->m_pWLSHM = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        g_pTrackpadColorPicker->m_mtTickMutex.lock();

        const auto PMONITOR    = g_pTrackpadColorPicker->m_vMonitors.emplace_back(std::make_unique<SMonitor>()).get();
        PMONITOR->wayland_name = name;
        PMONITOR->name         = "";
        PMONITOR->output       = (wl_output*)wl_registry_bind(registry, name, &wl_output_interface, 4);
        wl_output_add_listener(PMONITOR->output, &Events::outputListener, PMONITOR);

        if (g_pTrackpadColorPicker->m_pXDGOutputMgr) {
            auto xdg_output = zxdg_output_manager_v1_get_xdg_output(
                g_pTrackpadColorPicker->m_pXDGOutputMgr, PMONITOR->output);
            zxdg_output_v1_add_listener(xdg_output, &Events::xdgOutputListener, PMONITOR);
        }

        g_pTrackpadColorPicker->m_mtTickMutex.unlock();
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        g_pTrackpadColorPicker->m_pLayerShell = (zwlr_layer_shell_v1*)wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        g_pTrackpadColorPicker->createSeat((wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        g_pTrackpadColorPicker->m_pSCMgr = (zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        g_pTrackpadColorPicker->m_pCursorShape = (wp_cursor_shape_manager_v1*)wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1);
    }
}

void Events::handleGlobalRemove(void* data, struct wl_registry* registry, uint32_t name) {
    // todo
}

void Events::handlePointerButton(void* data, struct wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    auto fmax3 = [](float a, float b, float c) -> float { return (a > b && a > c) ? a : (b > c) ? b : c; };
    auto fmin3 = [](float a, float b, float c) -> float { return (a < b && a < c) ? a : (b < c) ? b : c; };

    // get the px and print it
    const auto MOUSECOORDSABS = g_pTrackpadColorPicker->m_vLastCoords.floor() / g_pTrackpadColorPicker->m_pLastSurface->m_pMonitor->size;
    const auto CLICKPOS       = MOUSECOORDSABS * g_pTrackpadColorPicker->m_pLastSurface->screenBuffer.pixelSize;

    const auto COL = g_pTrackpadColorPicker->getColorFromPixel(g_pTrackpadColorPicker->m_pLastSurface, CLICKPOS);

    // relative brightness of a color
    // https://www.w3.org/TR/2008/REC-WCAG20-20081211/#relativeluminancedef
    const auto FLUMI = [](const float& c) -> float { return c <= 0.03928 ? c / 12.92 : powf((c + 0.055) / 1.055, 2.4); };
    // threshold: (lumi_white + 0.05) / (x + 0.05) == (x + 0.05) / (lumi_black + 0.05)
    // https://www.w3.org/TR/2008/REC-WCAG20-20081211/#contrast-ratiodef
    const uint8_t FG = 0.2126 * FLUMI(COL.r / 255.0f) + 0.7152 * FLUMI(COL.g / 255.0f) + 0.0722 * FLUMI(COL.b / 255.0f) > 0.17913 ? 0 : 255;

    switch (g_pTrackpadColorPicker->m_bSelectedOutputMode) {
        case OUTPUT_CMYK: {
            // http://www.codeproject.com/KB/applications/xcmyk.aspx

            float r = 1 - (COL.r / 255.0f), g = 1 - (COL.g / 255.0f), b = 1 - (COL.b / 255.0f);
            float k = fmin3(r, g, b), K = (k == 1) ? 1 : 1 - k;
            float c = (r - k) / K, m = (g - k) / K, y = (b - k) / K;

            c = std::round(c * 100);
            m = std::round(m * 100);
            y = std::round(y * 100);
            k = std::round(k * 100);

            Clipboard::copy("%g%% %g%% %g%% %g%%", c, m, y, k);

            g_pTrackpadColorPicker->finish(1);
            break;
        }
        case OUTPUT_HEX: {
            auto toHex = [](int i) -> std::string {
                const char* DS = g_pTrackpadColorPicker->m_bUseLowerCase ? "0123456789abcdef" : "0123456789ABCDEF";

                std::string result;
                result += DS[i / 16];
                result += DS[i % 16];

                return result;
            };

            auto hexR = toHex(COL.r);
            auto hexG = toHex(COL.g);
            auto hexB = toHex(COL.b);

            Clipboard::copy("#%s%s%s", toHex(COL.r).c_str(), toHex(COL.g).c_str(), toHex(COL.b).c_str());

            g_pTrackpadColorPicker->finish(1);
            break;
        }
        case OUTPUT_RGB: {
            Clipboard::copy("%i %i %i", COL.r, COL.g, COL.b);

            g_pTrackpadColorPicker->finish(1);
            break;
        }
        case OUTPUT_HSL:
        case OUTPUT_HSV: {
            // https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB

            auto floatEq = [](float a, float b) -> bool {
                return std::nextafter(a, std::numeric_limits<double>::lowest()) <= b && std::nextafter(a, std::numeric_limits<double>::max()) >= b;
            };

            float h, s, l, v;
            float r = COL.r / 255.0f, g = COL.g / 255.0f, b = COL.b / 255.0f;
            float max = fmax3(r, g, b), min = fmin3(r, g, b);
            float c = max - min;

            v = max;
            if (c == 0)
                h = 0;
            else if (v == r)
                h = 60 * (0 + (g - b) / c);
            else if (v == g)
                h = 60 * (2 + (b - r) / c);
            else /* v == b */
                h = 60 * (4 + (r - g) / c);

            float l_or_v;
            if (g_pTrackpadColorPicker->m_bSelectedOutputMode == OUTPUT_HSL) {
                l      = (max + min) / 2;
                s      = (floatEq(l, 0.0f) || floatEq(l, 1.0f)) ? 0 : (v - l) / std::min(l, 1 - l);
                l_or_v = std::round(l * 100);
            } else {
                v      = max;
                s      = floatEq(v, 0.0f) ? 0 : c / v;
                l_or_v = std::round(v * 100);
            }

            h = std::round(h < 0 ? h + 360 : h);
            s = std::round(s * 100);

            Clipboard::copy("%g %g%% %g%%", h, s, l_or_v);

            g_pTrackpadColorPicker->finish(1);
            break;
        }
    }

    g_pTrackpadColorPicker->finish(1);
}

void Events::handleCapabilities(void* data, wl_seat* wl_seat, uint32_t capabilities) {
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        const auto POINTER = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(POINTER, &pointerListener, wl_seat);
        g_pTrackpadColorPicker->m_pCursorShapeDevice = wp_cursor_shape_manager_v1_get_pointer(g_pTrackpadColorPicker->m_pCursorShape, POINTER);
    } else {
        Debug::log(CRIT, "Trackpad-Color-Picker cannot work without a pointer!");
        g_pTrackpadColorPicker->finish(1);
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        wl_keyboard_add_listener(wl_seat_get_keyboard(wl_seat), &keyboardListener, wl_seat);
    }
}

void Events::handlePointerEnter(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    g_pTrackpadColorPicker->markDirty();
    wl_pointer_set_cursor(wl_pointer, 0, nullptr, 0, 0);

    g_pTrackpadColorPicker->m_vLastCoords = {wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y)};

    for (auto& ls : g_pTrackpadColorPicker->m_vLayerSurfaces) {
        g_pTrackpadColorPicker->renderSurface(ls.get(), true);
        if (ls->pSurface == surface) {
            g_pTrackpadColorPicker->m_pLastSurface = ls.get();

            if (!ls->pCursorImg)
                break;
        }
    }
    g_pTrackpadColorPicker->renderSurface(g_pTrackpadColorPicker->m_pLastSurface);
}

void Events::handlePointerLeave(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface) {
    for (auto& ls : g_pTrackpadColorPicker->m_vLayerSurfaces) {
        g_pTrackpadColorPicker->renderSurface(ls.get(), true);
    }
}

void Events::handlePointerAxis(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    // ignored
}

void Events::handlePointerMotion(void* data, struct wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    auto x = wl_fixed_to_double(surface_x);
    auto y = wl_fixed_to_double(surface_y);

    g_pTrackpadColorPicker->m_vLastCoords = {x, y};

    g_pTrackpadColorPicker->markDirty();
}

void Events::handleKeyboardKeymap(void* data, wl_keyboard* wl_keyboard, uint format, int fd, uint size) {
    if (!g_pTrackpadColorPicker->m_pXKBContext)
        return;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        Debug::log(ERR, "Could not recognise keymap format");
        return;
    }

    const char* buf = (const char*)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        Debug::log(ERR, "Failed to mmap xkb keymap: %d", errno);
        return;
    }

    g_pTrackpadColorPicker->m_pXKBKeymap = xkb_keymap_new_from_buffer(g_pTrackpadColorPicker->m_pXKBContext, buf, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

    munmap((void*)buf, size);
    close(fd);

    if (!g_pTrackpadColorPicker->m_pXKBKeymap) {
        Debug::log(ERR, "Failed to compile xkb keymap");
        return;
    }

    g_pTrackpadColorPicker->m_pXKBState = xkb_state_new(g_pTrackpadColorPicker->m_pXKBKeymap);
    if (!g_pTrackpadColorPicker->m_pXKBState) {
        Debug::log(ERR, "Failed to create xkb state");
        return;
    }
}

void Events::handleKeyboardKey(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    if (g_pTrackpadColorPicker->m_pXKBState) {
        if (xkb_state_key_get_one_sym(g_pTrackpadColorPicker->m_pXKBState, key + 8) == XKB_KEY_Escape)
            g_pTrackpadColorPicker->finish();
    } else if (key == 1) // Assume keycode 1 is escape
        g_pTrackpadColorPicker->finish();
}

void Events::handleKeyboardEnter(void* data, wl_keyboard* wl_keyboard, uint serial, wl_surface* surface, wl_array* keys) {}

void Events::handleKeyboardLeave(void* data, wl_keyboard* wl_keyboard, uint serial, wl_surface* surface) {}

void Events::handleKeyboardModifiers(void* data, wl_keyboard* wl_keyboard, uint serial, uint mods_depressed, uint mods_latched, uint mods_locked, uint group) {
    if (!g_pTrackpadColorPicker->m_pXKBState)
        return;

    xkb_state_update_mask(g_pTrackpadColorPicker->m_pXKBState, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

void Events::handleFrameDone(void* data, struct wl_callback* callback, uint32_t time) {
    CLayerSurface* pLS = (CLayerSurface*)data;

    if (pLS->frame_callback)
        wl_callback_destroy(pLS->frame_callback);

    pLS->frame_callback = nullptr;

    if (pLS->dirty || !pLS->rendered)
        g_pTrackpadColorPicker->renderSurface(g_pTrackpadColorPicker->m_pLastSurface);
}

void Events::handleBufferRelease(void* data, struct wl_buffer* wl_buffer) {
    auto buf  = (SPoolBuffer*)data;
    buf->busy = false;
}

void Events::handleSCBuffer(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    const auto PLS = (CLayerSurface*)data;

    PLS->screenBufferFormat = format;

    if (!PLS->screenBuffer.buffer)
        g_pTrackpadColorPicker->createBuffer(&PLS->screenBuffer, width, height, format, stride);

    zwlr_screencopy_frame_v1_copy(frame, PLS->screenBuffer.buffer);
}

void Events::handleSCFlags(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t flags) {
    const auto PLS = (CLayerSurface*)data;

    PLS->scflags = flags;

    g_pTrackpadColorPicker->recheckACK();
}

void Events::handleSCReady(void* lsdata, struct zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto  PLS = (CLayerSurface*)lsdata;

    SPoolBuffer newBuf;
    Vector2D    transformedSize = PLS->screenBuffer.pixelSize;

    if (PLS->m_pMonitor->transform % 2 == 1)
        std::swap(transformedSize.x, transformedSize.y);

    g_pTrackpadColorPicker->createBuffer(&newBuf, transformedSize.x, transformedSize.y, PLS->screenBufferFormat, transformedSize.x * 4);

    int   bytesPerPixel = PLS->screenBuffer.stride / (int)PLS->screenBuffer.pixelSize.x;
    void* data          = PLS->screenBuffer.data;
    if (bytesPerPixel == 4)
        g_pTrackpadColorPicker->convertBuffer(&PLS->screenBuffer);
    else if (bytesPerPixel == 3) {
        Debug::log(WARN, "24 bit formats are unsupported, Trackpad-Color-Picker may or may not work as intended!");
        data                         = g_pTrackpadColorPicker->convert24To32Buffer(&PLS->screenBuffer);
        PLS->screenBuffer.paddedData = data;
    } else {
        Debug::log(CRIT, "Unsupported stride/bytes per pixel %i", bytesPerPixel);
        g_pTrackpadColorPicker->finish(1);
    }

    cairo_surface_t* oldSurface = cairo_image_surface_create_for_data((unsigned char*)data, CAIRO_FORMAT_ARGB32, PLS->screenBuffer.pixelSize.x, PLS->screenBuffer.pixelSize.y,
                                                                      PLS->screenBuffer.pixelSize.x * 4);

    cairo_surface_flush(oldSurface);

    newBuf.surface = cairo_image_surface_create_for_data((unsigned char*)newBuf.data, CAIRO_FORMAT_ARGB32, transformedSize.x, transformedSize.y, transformedSize.x * 4);

    const auto PCAIRO = cairo_create(newBuf.surface);

    auto       cairoTransformMtx = [&](cairo_matrix_t* mtx) -> void {
        const auto TR = PLS->m_pMonitor->transform % 4;

        if (TR == 0)
            return;

        cairo_matrix_rotate(mtx, -M_PI_2 * (double)TR);

        if (TR == 1)
            cairo_matrix_translate(mtx, -transformedSize.x, 0);
        else if (TR == 2)
            cairo_matrix_translate(mtx, -transformedSize.x, -transformedSize.y);
        else if (TR == 3)
            cairo_matrix_translate(mtx, 0, -transformedSize.y);

        // TODO: flipped
    };

    cairo_save(PCAIRO);

    cairo_set_source_rgba(PCAIRO, 0, 0, 0, 0);

    cairo_rectangle(PCAIRO, 0, 0, 0xFFFF, 0xFFFF);
    cairo_fill(PCAIRO);

    const auto PATTERNPRE = cairo_pattern_create_for_surface(oldSurface);
    cairo_pattern_set_filter(PATTERNPRE, CAIRO_FILTER_BILINEAR);
    cairo_matrix_t matrixPre;
    cairo_matrix_init_identity(&matrixPre);
    cairo_matrix_scale(&matrixPre, 1.0, 1.0);
    cairoTransformMtx(&matrixPre);
    cairo_pattern_set_matrix(PATTERNPRE, &matrixPre);
    cairo_set_source(PCAIRO, PATTERNPRE);
    cairo_paint(PCAIRO);

    cairo_surface_flush(newBuf.surface);

    cairo_pattern_destroy(PATTERNPRE);

    cairo_destroy(PCAIRO);

    cairo_surface_destroy(oldSurface);

    g_pTrackpadColorPicker->destroyBuffer(&PLS->screenBuffer);

    PLS->screenBuffer = newBuf;

    g_pTrackpadColorPicker->renderSurface(PLS);
}

void Events::handleSCFailed(void* data, struct zwlr_screencopy_frame_v1* frame) {
    Debug::log(CRIT, "Failed to get a Screencopy!");
    g_pTrackpadColorPicker->finish(1);
}
