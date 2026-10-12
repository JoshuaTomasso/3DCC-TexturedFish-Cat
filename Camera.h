// Requires Gateware MATH & INPUT
// Simple free look camera with only main window provided
// Just include and call once each frame in your update/render loop
#include "Gateware.h"
#ifndef _CAMERA_H_
#define _CAMERA_H_

// Initializes a camera matrix and then updates it based on user input
// Give it your window and starting view space camera matrix (optional)
// It will return a time/input modified version in view space
GW::MATH::GMATRIXF FreeLookCamera(  GW::SYSTEM::GWindow _window,
                                    GW::MATH::GMATRIXF _viewStart =
                                    GW::MATH::GIdentityMatrixF)
{
    static GW::INPUT::GInput keym;
    static GW::INPUT::GController ctrl;
    static GW::CORE::GEventResponder free;
    static GW::MATH::GMATRIXF view = GW::MATH::GIdentityMatrixF;
    static auto once = [&]()-> bool {
        GW::MATH::GMatrix mlib;
        view = _viewStart; // FYI: not in world space
		ctrl.Create(); // optional, only if you have a controller
        if (-keym.Create(_window) || -mlib.Create()) {
            throw std::runtime_error("Input and/or Math libraries not available!");
        }
        // prevents crash when stack unwinds
        free.Create([&](const GW::GEvent& e) {
			GW::SYSTEM::GWindow::Events q;
            if (+e.Read(q) && q == GW::SYSTEM::GWindow::Events::DESTROY) {
                keym = nullptr;
                ctrl = nullptr;
            }
		});
        _window.Register(free);
        return true;
    };
    static bool init = once();
    static auto start = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    start = std::chrono::steady_clock::now();
    
    GW::MATH::GMATRIXF cam = GW::MATH::GIdentityMatrixF;
    GW::MATH::GMatrix::InverseF(view, cam);
    float states[6] = { 0, 0, 0, 0, 0, 0 };
    static double Camera_Speed = 0.75f;
    keym.GetState(G_KEY_SPACE, states[0] = 0);
    keym.GetState(G_KEY_LEFTSHIFT, states[1] = 0);
    ctrl.GetState(0, G_RIGHT_TRIGGER_AXIS, states[2] = 0);
    ctrl.GetState(0, G_LEFT_TRIGGER_AXIS, states[3] = 0);
    double Y_Change = states[0] - states[1] + states[2] - states[3];
    cam.row4.y += static_cast<float>(Y_Change * Camera_Speed * elapsed);
    const float pfs = Camera_Speed * elapsed;
    keym.GetState(G_KEY_W, states[0] = 0);
    keym.GetState(G_KEY_S, states[1] = 0);
    keym.GetState(G_KEY_D, states[2] = 0);
    keym.GetState(G_KEY_A, states[3] = 0);
    ctrl.GetState(0, G_LY_AXIS, states[4] = 0);
    ctrl.GetState(0, G_LX_AXIS, states[5] = 0);
    double Z_Change = states[0] - states[1] + states[4];
    double X_Change = states[2] - states[3] + states[5];
    GW::MATH::GMatrix::TranslateLocalF(cam,
        GW::MATH::GVECTORF{ static_cast<float>(X_Change * pfs),
        0, static_cast<float>(Z_Change * pfs) }, cam);
    bool focused;
    unsigned height;
    _window.IsFocus(focused);
    _window.GetClientHeight(height);
    if (keym.GetMouseDelta(states[0] = 0, states[1] = 0) 
        != GW::GReturn::SUCCESS || !focused) {
        states[0] = states[1] = 0; // don't keep spinning
    }
    ctrl.GetState(0, G_RY_AXIS, states[2] = 0);
    ctrl.GetState(0, G_RX_AXIS, states[3] = 0);
    double ThumbSpeed = G_PI * elapsed;
    double Pitch = G_PI / 2 * states[1] / height + states[2] * -ThumbSpeed;
    GW::MATH::GMATRIXF mPitch;
    GW::MATH::GMatrix::RotateXLocalF(GW::MATH::GIdentityMatrixF, Pitch, mPitch);
    GW::MATH::GMatrix::MultiplyMatrixF(mPitch, cam, cam);
    unsigned width;
    _window.GetClientWidth(width);
    double ar = (width/static_cast<double>(height));
    double Yaw = G_PI / 2 * ar * states[0] / width + states[3] * ThumbSpeed;
    GW::MATH::GMATRIXF mYaw;
    GW::MATH::GMatrix::RotateYLocalF(GW::MATH::GIdentityMatrixF, Yaw, mYaw);
    GW::MATH::GVECTORF pos = cam.row4;
    GW::MATH::GMatrix::MultiplyMatrixF(cam, mYaw, cam);
    cam.row4 = pos;
    GW::MATH::GMatrix::InverseF(cam, view);

   
    // row major camera matrix in view space, send to shaders
    return view;
}

#endif /* Camera_h */
