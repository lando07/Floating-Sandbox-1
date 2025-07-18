/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2022-04-23
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include <Render/RenderContext.h>

#include <Core/AABBSet.h>
#include <Core/ParameterSmoother.h>
#include <Core/Vectors.h>

#include <optional>

class ViewManager
{
public:

    ViewManager(
        std::optional<AutoFocusTargetKindType> autoFocusTarget,
        RenderContext & renderContext);

    float GetCameraSpeedAdjustment() const;
    void SetCameraSpeedAdjustment(float value);
    static float constexpr GetMinCameraSpeedAdjustment() { return 0.2f; }
    static float constexpr GetMaxCameraSpeedAdjustment() { return 10.0f; }

    bool GetDoAutoFocusOnShipLoad() const;
    void SetDoAutoFocusOnShipLoad(bool value);

    bool GetDoAutoFocusOnNpcPlacement() const;
    void SetDoAutoFocusOnNpcPlacement(bool value);

    std::optional<AutoFocusTargetKindType> GetAutoFocusTarget() const;
    void SetAutoFocusTarget(std::optional<AutoFocusTargetKindType> const & target);

    void OnViewModelUpdated();
    void Pan(vec2f const & worldOffset);
    void PanToWorldX(float worldX);
    void PanToWorldY(float worldY);
    void AdjustZoom(float amount);
    void FocusOn(
        Geometry::AABB const & aabb,
        float widthMultiplier,
        float heightMultiplier,
        float zoomToleranceMultiplierMin,
        float zoomToleranceMultiplierMax,
        bool anchorAabbCenterAtCurrentScreenPosition);

    void UpdateAutoFocus(std::optional<Geometry::AABB> const & aabb);
    void Update();

    // Removes user offsets and returns to "pure autofocus"
    void ResetAutoFocusAlterations();

    // Sets limits on auto-focus for ships
    void SetAutoFocusOnShipLimitAABB(Geometry::AABB const & aabb);

private:

    static float CalculateParameterSmootherConvergenceFactor(float cameraSpeedAdjustment);

    static inline float GetMaxZoomForTarget(std::optional<AutoFocusTargetKindType> targetKind);

    void InternalFocusOn(
        Geometry::AABB const & aabb,
        float widthMultiplier,
        float heightMultiplier,
        float zoomToleranceMultiplierMin,
        float zoomToleranceMultiplierMax,
        bool anchorAabbCenterAtCurrentScreenPosition);

    float InternalCalculateZoom(
        Geometry::AABB const & aabb,
        float widthMultiplier,
        float heightMultiplier,
        std::optional<AutoFocusTargetKindType> targetKind) const;

    static inline float InternalCalculateZoomForAABB(
        Geometry::AABB const & aabb,
        float widthMultiplier,
        float heightMultiplier,
        RenderContext const & renderContext);

    inline float InternalCalculateAutoFocusOnShipMaxZoom(Geometry::AABB const & aabb) const;

private:

    RenderContext & mRenderContext;

    float mCameraSpeedAdjustment; // Storage
    bool mDoAutoFocusOnShipLoad; // Storage
    bool mDoAutoFocusOnNpcPlacement; // Storage

    ParameterSmoother<float> mInverseZoomParameterSmoother; // Smooths 1/zoom, which is effectively the Z coord
    ParameterSmoother<vec2f> mCameraWorldPositionParameterSmoother;
    float mCameraWorldPositionParameterSmootherContingentMultiplier; // One shot, always reset to 1.0 after use

    struct AutoFocusSessionData
    {
        std::optional<AutoFocusTargetKindType> AutoFocusTarget; // Here so it's consistent, and can use target type to fine tune focus

        float CurrentAutoFocusZoom;
        vec2f CurrentAutoFocusCameraWorldPosition;

        float UserZoomOffset;
        vec2f UserCameraWorldPositionOffset;

        AutoFocusSessionData(
            std::optional<AutoFocusTargetKindType> const & autoFocusTarget,
            float currentAutoFocusZoom,
            vec2f const & currentAutoFocusCameraWorldPosition)
            : AutoFocusTarget(autoFocusTarget)
            , CurrentAutoFocusZoom(currentAutoFocusZoom)
            , CurrentAutoFocusCameraWorldPosition(currentAutoFocusCameraWorldPosition)
        {
            ResetUserOffsets();
        }

        void ResetUserOffsets()
        {
            UserZoomOffset = 1.0f;
            UserCameraWorldPositionOffset = vec2f::zero();
        }
    };

    std::optional<AutoFocusSessionData> mAutoFocus; // When set, we're doing auto-focus

    // Limits for auto-focus - imposed from outside
    std::optional<Geometry::AABB> mAutoFocusOnShipLimitAABB; // Stored so we may recalculate on view model changed
    std::optional<float> mAutoFocusOnShipMaxZoom; // Max magnification - we won't magnify more than this
};
