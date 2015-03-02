#pragma once

#include <stdexcept>
#include <utility>
#include <OVR.h>
#include <OVR_CAPI_GL.h>
#include <vlCore/Vector2.hpp>
#include <vlCore/Matrix4.hpp>
#include <vlCore/math_utils.hpp>
#include <vlGraphics/Texture.hpp>
#include <vlGraphics/Framebuffer.hpp>
#include <vlGraphics/FramebufferObject.hpp>
#include <vlGraphics/OpenGLContext.hpp>

class OVRManager
{
public:
    OVRManager()
    {
    }

    ~OVRManager()
    {
    }
    static void init()
    {
        ovr_Initialize();
    }

    static void finish()
    {
        ovr_Shutdown();
    }

    void prepereOVR(HWND hWnd, HDC dc, bool directMode= true)
    {
        createDevice();
        configureTracking();
        configureRendering(hWnd, dc);
        ovrHmd_SetEnabledCaps(m_hmd, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction);
        if (directMode)
        {
            directRenderingTo(hWnd);
        }
    }

    void getHmdPose(ovrPosef outEyePoses[2])
    {
        ovrTrackingState outHmdTrackingState;
        getHmdPose(outEyePoses, &outHmdTrackingState);
    }

    void getHmdPose(ovrPosef outEyePoses[2], ovrTrackingState* outHmdTrackingState, unsigned int frameIdx = 0)
    {

        ovrVector3f hmdToEyeViewOffset[2] = {
            getEyeRenderDesc(ovrEye_Left).HmdToEyeViewOffset,
            getEyeRenderDesc(ovrEye_Right).HmdToEyeViewOffset,
        };
        ovrHmd_GetEyePoses(m_hmd, frameIdx, hmdToEyeViewOffset,
                                   outEyePoses,
                                   outHmdTrackingState);
    }

    ovrMatrix4f getEyeProjection(ovrEyeType eye, float nearPlane = 0.01f, float farPlane = 10000.0f)
    {
        return ovrMatrix4f_Projection(getEyeRenderDesc(eye).Fov, nearPlane, farPlane, true);
    }

    ovrEyeRenderDesc getEyeRenderDesc(ovrEyeType t)
    {
        return ovrHmd_GetRenderDesc(m_hmd, t, m_hmd->DefaultEyeFov[t]);
    }



    ovrRecti getEyeRenderViewport(int idx)
    {
        ovrRecti viewport;
        if (idx == 0)
        {
            viewport.Pos = OVR::Vector2i(0, 0);
            viewport.Size = OVR::Sizei((getRenderTargetSizeOvr().w + 1)/2, getRenderTargetSizeOvr().h);
            return viewport;
        }
        else
        {
            viewport.Pos = OVR::Vector2i((getRenderTargetSizeOvr().w + 1) / 2, 0);
            viewport.Size = OVR::Sizei((getRenderTargetSizeOvr().w + 1)/2, getRenderTargetSizeOvr().h);
            return viewport;
        }
    }

    vl::ref<vl::FramebufferObject> createRenderTarget(vl::OpenGLContext* ctx, bool depth, bool stencil)
    {
        auto targetTex = getRenderTexture();
        vl::ref<vl::FramebufferObject> fbo = ctx->createFramebufferObject(targetTex->width(), targetTex->height());
        fbo->addTextureAttachment(
            vl::AP_COLOR_ATTACHMENT0,
            new vl::FBOTexture2DAttachment(
                targetTex.get(), 0, vl::ETex2DTarget::T2DT_TEXTURE_2D));
        if (depth && stencil)
        {
            if (vl::Has_GL_EXT_packed_depth_stencil)
            {
                vl::Log::print(vl::Say("with packed depth stencil\n"));
                fbo->addDepthStencilAttachment(new vl::FBODepthStencilBufferAttachment());
            }
            else
            {
                vl::Log::print(vl::Say("without packed depth stencil\n"));
                fbo->addDepthAttachment(new vl::FBODepthBufferAttachment(vl::DBF_DEPTH_COMPONENT24));
                fbo->addStencilAttachment(new vl::FBOStencilBufferAttachment(vl::SBF_STENCIL_INDEX8));
            }
        }
        else
        {
            if (depth)
            {
                fbo->addDepthAttachment(new vl::FBODepthBufferAttachment(vl::DBF_DEPTH_COMPONENT24));
            }
            if (stencil)
            {
                fbo->addStencilAttachment(new vl::FBOStencilBufferAttachment(vl::SBF_STENCIL_INDEX8));
            }
        }
        return fbo;

    }

    ovrGLTexture getEyeTexture(int eye)
    {
        auto tex = getRenderTexture();
        ovrGLTexture ovrTex;
        ovrTex.OGL.Header.API = ovrRenderAPI_OpenGL;
        ovrTex.OGL.Header.TextureSize = getRenderTargetSizeOvr();
        ovrTex.OGL.Header.RenderViewport = getEyeRenderViewport(eye);
        ovrTex.OGL.TexId = tex->handle();
        return ovrTex;
    }
    std::array<ovrGLTexture, 2> getEyeTextures()
    {
        auto tex = getRenderTexture();
        std::array<ovrGLTexture, 2> ovrTex;
        ovrTex[0] = getEyeTexture(0);
        ovrTex[1] = getEyeTexture(1);
        return ovrTex;
    }


    vl::mat4 getTrackingState()
    {
        ovrTrackingState ts = ovrHmd_GetTrackingState(m_hmd, ovr_GetTimeInSeconds());
        if (ts.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked))
        {
            ovrPosef pose = ts.HeadPose.ThePose;
            vl::mat4 tracking;
            auto m = OVR::Matrix4f(pose);
            return vl::mat4(&(m.M[0][0]));
        }
        else
        {
            return vl::mat4();
        }
    }

    void dismissHSW()
    {
        // Health and Safety Warning display state.
        ovrHSWDisplayState hswDisplayState;
        ovrHmd_GetHSWDisplayState(m_hmd, &hswDisplayState);
        if (hswDisplayState.Displayed)
        {
            // Dismiss the warning if the user pressed the appropriate key or if the user
            // is tapping the side of the HMD.
            // If the user has requested to dismiss the warning via keyboard or controller input...
            {
                // Detect a moderate tap on the side of the HMD.
                ovrTrackingState ts = ovrHmd_GetTrackingState(m_hmd, ovr_GetTimeInSeconds());
                if (ts.StatusFlags & ovrStatus_OrientationTracked)
                {
                    const OVR::Vector3f v(
                        ts.RawSensorData.Accelerometer.x,
                        ts.RawSensorData.Accelerometer.y,
                        ts.RawSensorData.Accelerometer.z);
                    // Arbitrary value and representing moderate tap on the side of the DK2 Rift.
                    if (v.LengthSq() > 250.f)
                        ovrHmd_DismissHSWDisplay(m_hmd);
                }
            }
        }
    }

    ovrEyeType getEyeRenderOrder(int eyeIndex)
    {
        return m_hmd->EyeRenderOrder[eyeIndex];
    }

    ovrHmd getHmd()
    {
        return m_hmd;
    }

    void directRenderingTo(HWND window)
    {
        ovrHmd_AttachToWindow(m_hmd, window, nullptr, nullptr);
    }

    void doRenderHmd(std::function<void(ovrEyeType, ovrRecti const& viewport, OVR::Matrix4f const& proj)> renderCallback)
    {
        using namespace OVR;
        ovrPosef headPose[2];
        getHmdPose(headPose);
        dismissHSW();
        for (int eyeIndex = 0; eyeIndex < ovrEye_Count; eyeIndex++)
        {
            ovrEyeType eye = getEyeRenderOrder(eyeIndex);
            Quatf orientation = Quatf(headPose[eye].Orientation);

            Matrix4f proj = getEyeProjection(eye);
            proj.Transpose();
            // * Test code *
            // Assign quaternion result directly to view (translation is ignored).
            //Matrix4f view = Matrix4f(orientation.Inverted()) * Matrix4f::Translation(-WorldEyePos);
            auto renderViewport = getEyeRenderViewport(eye);
            renderCallback(eye, renderViewport, proj);
        }
        
        auto eyeTex = getEyeTextures();
        ovrHmd_EndFrame(getHmd(), headPose, (const ovrTexture*)eyeTex.data());
    }

private:
    void createDevice()
    {
        m_hmd = ovrHmd_Create(0);
        if (!m_hmd)
        {
            throw std::runtime_error("can't initialized OVR");
        }
    }

    void destroyDevice()
    {
        if (m_hmd)
        {
            ovrHmd_Destroy(m_hmd);
        }
    }
    void configureTracking()
    {
        ovrHmd_ConfigureTracking(m_hmd, ovrTrackingCap_Orientation |
            ovrTrackingCap_MagYawCorrection |
            ovrTrackingCap_Position, 0);
    }

    void configureRendering(HWND hWnd, HDC dc)
    {
        // Configure OpenGL.
        // Initialize ovrEyeRenderDesc struct.
        ovrFovPort eyeFov[2];
        std::memcpy(eyeFov, m_hmd->DefaultEyeFov, sizeof(eyeFov));
        ovrGLConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.OGL.Header.API = ovrRenderAPI_OpenGL;
        cfg.OGL.Header.BackBufferSize.w = m_hmd->Resolution.w;
        cfg.OGL.Header.BackBufferSize.h = m_hmd->Resolution.h;
        cfg.OGL.Header.Multisample = 0;
        cfg.OGL.Window = hWnd;
        cfg.OGL.DC = dc;
        ovrEyeRenderDesc eyeRenderDesc[2];
        ovrBool result = ovrHmd_ConfigureRendering(m_hmd, &cfg.Config, ovrDistortionCap_Chromatic |
                                                        ovrDistortionCap_TimeWarp |
                                                        ovrDistortionCap_Overdrive,
                                                        eyeFov, eyeRenderDesc);
    }
    vl::uvec2 getRenderTargetSize()
    {
        // Configure Stereo settings.
        ovrSizei recommenedTex0Size = ovrHmd_GetFovTextureSize(m_hmd, ovrEye_Left,
            m_hmd->DefaultEyeFov[0], 1.0f);
        ovrSizei recommenedTex1Size = ovrHmd_GetFovTextureSize(m_hmd, ovrEye_Right,
            m_hmd->DefaultEyeFov[1], 1.0f);
        ovrSizei renderTargetSize;
        renderTargetSize.w = recommenedTex0Size.w + recommenedTex1Size.w;
        renderTargetSize.h = std::max( recommenedTex0Size.h, recommenedTex1Size.h );
        const int eyeRenderMultisample = 1;
        m_renderTargetSize = vl::uvec2(renderTargetSize.w, renderTargetSize.h);
        return m_renderTargetSize;
    }

    ovrSizei getRenderTargetSizeOvr()
    {
        // Configure Stereo settings.
        ovrSizei recommenedTex0Size = ovrHmd_GetFovTextureSize(m_hmd, ovrEye_Left,
            m_hmd->DefaultEyeFov[0], 1.0f);
        ovrSizei recommenedTex1Size = ovrHmd_GetFovTextureSize(m_hmd, ovrEye_Right,
            m_hmd->DefaultEyeFov[1], 1.0f);
        ovrSizei renderTargetSize;
        renderTargetSize.w = recommenedTex0Size.w + recommenedTex1Size.w;
        renderTargetSize.h = std::max( recommenedTex0Size.h, recommenedTex1Size.h );
        const int eyeRenderMultisample = 1;
        return renderTargetSize;
    }
    vl::ref<vl::Texture> getRenderTexture()
    {
        if (mTargetTex.get())
        {
            return mTargetTex;
        }
        auto texSize = getRenderTargetSize();
        mTargetTex = new vl::Texture();
        mTargetTex->createTexture2D(texSize.x(), texSize.y(), vl::TF_RGB, false);
        return mTargetTex;
    }

    vl::uvec2 m_renderTargetSize;
    vl::ref<vl::Texture> mTargetTex;
    ovrHmd m_hmd;
};