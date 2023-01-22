﻿/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2018-04-14
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#include "Physics.h"

#include <GameCore/Algorithms.h>
#include <GameCore/GameRandomEngine.h>
#include <GameCore/GameWallClock.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Physics {

float constexpr MaxInteractiveWaveAbsRelativeHeight = 6.0f;

// The number of slices we want to render the water surface as;
// this is our graphical resolution
template<typename T>
T constexpr RenderSlices = 768;

static std::chrono::seconds constexpr TsunamiGracePeriod(120);
static std::chrono::seconds constexpr RogueWaveGracePeriod(5);

OceanSurface::OceanSurface(
    World & parentWorld,
    std::shared_ptr<GameEventDispatcher> gameEventDispatcher)
    : mParentWorld(parentWorld)
    , mGameEventHandler(std::move(gameEventDispatcher))
    ////////
    , mBasalWaveAmplitude1(0.0f)
    , mBasalWaveAmplitude2(0.0f)
    , mBasalWaveNumber1(0.0f)
    , mBasalWaveNumber2(0.0f)
    , mBasalWaveAngularVelocity1(0.0f)
    , mBasalWaveAngularVelocity2(0.0f)
    , mBasalWaveSin1()
    , mNextTsunamiTimestamp(GameWallClock::duration::max())
    , mNextRogueWaveTimestamp(GameWallClock::duration::max())
    ////////
    , mWindBaseAndStormSpeedMagnitude(std::numeric_limits<float>::max())
    , mBasalWaveHeightAdjustment(std::numeric_limits<float>::max())
    , mBasalWaveLengthAdjustment(std::numeric_limits<float>::max())
    , mBasalWaveSpeedAdjustment(std::numeric_limits<float>::max())
    , mTsunamiRate(std::chrono::minutes::max())
    , mRogueWaveRate(std::chrono::seconds::max())
    ////////
    , mSamples(SamplesCount + 1)
    , mSWEHeightField(SWEBufferAlignmentPrefixSize + SWEBoundaryConditionsSamples + SamplesCount + SWEBoundaryConditionsSamples)
    , mSWEVelocityField(SWEBufferAlignmentPrefixSize + SWEBoundaryConditionsSamples + SamplesCount + SWEBoundaryConditionsSamples + 1)
    , mInteractiveWaveTargetHeight(SamplesCount)
    , mInteractiveWaveCurrentHeightGrowthCoefficient(SamplesCount)
    , mInteractiveWaveTargetHeightGrowthCoefficient(SamplesCount)
    , mInteractiveWaveHeightGrowthCoefficientGrowthRate(SamplesCount)
    , mDeltaHeightBuffer(DeltaHeightBufferAlignmentPrefixSize + (DeltaHeightSmoothing / 2) + SamplesCount + (DeltaHeightSmoothing / 2))
    ////////
    , mSWETsunamiWaveStateMachine()
    , mSWERogueWaveWaveStateMachine()
    , mLastTsunamiTimestamp(GameWallClock::GetInstance().Now())
    , mLastRogueWaveTimestamp(GameWallClock::GetInstance().Now())
{
    // Initialize buffers
    mSamples.fill({ 0.0f, 0.0f });
    mSWEHeightField.fill(SWEHeightFieldOffset);
    mSWEVelocityField.fill(0.0f);
    mInteractiveWaveTargetHeight.fill(SWEHeightFieldOffset);
    mInteractiveWaveCurrentHeightGrowthCoefficient.fill(0.0f);
    mInteractiveWaveTargetHeightGrowthCoefficient.fill(0.0f);
    mInteractiveWaveHeightGrowthCoefficientGrowthRate.fill(0.0f);
    mDeltaHeightBuffer.fill(0.0f);

    // Initialize constant sample values
    mSamples[SamplesCount - 1].SampleValuePlusOneMinusSampleValue = 0.0f; // Extra sample is always == last sample
    mSamples[SamplesCount].SampleValuePlusOneMinusSampleValue = 0.0f; // Won't really be used
}

void OceanSurface::Update(
    float currentSimulationTime,
    Wind const & wind,
    GameParameters const & gameParameters)
{
    auto const now = GameWallClock::GetInstance().Now();

    //
    // Check whether parameters have changed
    //

    if (mWindBaseAndStormSpeedMagnitude != wind.GetBaseAndStormSpeedMagnitude()
        || mBasalWaveHeightAdjustment != gameParameters.BasalWaveHeightAdjustment
        || mBasalWaveLengthAdjustment != gameParameters.BasalWaveLengthAdjustment
        || mBasalWaveSpeedAdjustment != gameParameters.BasalWaveSpeedAdjustment)
    {
        RecalculateWaveCoefficients(wind, gameParameters);
    }

    if (mTsunamiRate != gameParameters.TsunamiRate
        || mRogueWaveRate != gameParameters.RogueWaveRate)
    {
        RecalculateAbnormalWaveTimestamps(gameParameters);
    }

    //
    // 1. Advance SWE Wave State Machines
    //

    // Tsunami
    if (mSWETsunamiWaveStateMachine.has_value())
    {
        if (currentSimulationTime > mSWETsunamiWaveStateMachine->GetStartSimulationTime() + 5.0f)
        {
            // Done
            mSWETsunamiWaveStateMachine.reset();
        }
        else
        {
            // Apply
            ImpartInteractiveWave(
                mSWETsunamiWaveStateMachine->GetCenterX(),
                mSWETsunamiWaveStateMachine->GetTargetRelativeHeight(),
                mSWETsunamiWaveStateMachine->GetRate(),
                0.0f);
        }
    }
    else
    {
        //
        // See if it's time to generate a tsunami
        //

        if (now > mNextTsunamiTimestamp)
        {
            // Tsunami!
            TriggerTsunami(currentSimulationTime);

            mLastTsunamiTimestamp = now;

            // Reset automatically-generated tsunamis
            mNextTsunamiTimestamp = CalculateNextAbnormalWaveTimestamp(
                now,
                gameParameters.TsunamiRate,
                TsunamiGracePeriod);

            // Tell world
            mParentWorld.DisturbOcean(std::chrono::milliseconds(0));
        }
    }

    // Rogue Wave
    if (mSWERogueWaveWaveStateMachine.has_value())
    {
        if (currentSimulationTime > mSWERogueWaveWaveStateMachine->GetStartSimulationTime() + 2.0f)
        {
            // Done
            mSWERogueWaveWaveStateMachine.reset();
        }
        else
        {
            // Apply
            ImpartInteractiveWave(
                mSWERogueWaveWaveStateMachine->GetCenterX(),
                mSWERogueWaveWaveStateMachine->GetTargetRelativeHeight(),
                mSWERogueWaveWaveStateMachine->GetRate(),
                0.0f);
        }
    }
    else
    {
        //
        // See if it's time to generate a rogue wave
        //

        if (now > mNextRogueWaveTimestamp)
        {
            // RogueWave!
            TriggerRogueWave(currentSimulationTime, wind);

            mLastRogueWaveTimestamp = now;

            // Reset automatically-generated rogue waves
            mNextRogueWaveTimestamp = CalculateNextAbnormalWaveTimestamp(
                now,
                gameParameters.RogueWaveRate,
                RogueWaveGracePeriod);
        }
    }

    //
    // 2. Interactive Waves Update
    //

    UpdateInteractiveWaves();

    //
    // 3. SWE Update
    //

    SmoothDeltaBufferIntoHeightField();

    ApplyDampingBoundaryConditions();

    //AdvectFieldsTest();

    UpdateFields(gameParameters);

    //
    // 4. Generate Samples
    //

    GenerateSamples(
        currentSimulationTime,
        wind,
        gameParameters);

    //
    // 5. Reset Interactive Waves
    //

    ResetInteractiveWaves();
}

void OceanSurface::Upload(Render::RenderContext & renderContext) const
{
    switch (renderContext.GetOceanRenderDetail())
    {
        case OceanRenderDetailType::Basic:
        {
            InternalUpload<OceanRenderDetailType::Basic>(renderContext);

            break;
        }

        case OceanRenderDetailType::Detailed:
        {
            InternalUpload<OceanRenderDetailType::Detailed>(renderContext);

            break;
        }
    }
}

void OceanSurface::AdjustTo(
    vec2f const & worldCoordinates,
    float worldRadius)
{
    // Calculate desired height
    float const targetRelativeHeight = Clamp(worldCoordinates.y / SWEHeightFieldAmplification, -MaxInteractiveWaveAbsRelativeHeight, MaxInteractiveWaveAbsRelativeHeight);

    // Calculate the growth rate for the height growth coefficient; we want small waves to raise fast
    // and tall waves to raise slow; our formula is thus:
    // AsymptoticRate + (1.0 - AsymptoticRate) * alpha^2 / (h + alpha)^2
    float constexpr AsymptoticRate = 0.0001f;
    float const heightGrowthCoefficientGrowthRate = AsymptoticRate + (1.0f - AsymptoticRate) * (0.1f * 0.1f / ((std::abs(targetRelativeHeight) + 0.1f) * (std::abs(targetRelativeHeight) + 0.1f)));

    ImpartInteractiveWave(
        worldCoordinates.x,
        targetRelativeHeight,
        heightGrowthCoefficientGrowthRate,
        worldRadius);
}

void OceanSurface::ApplyThanosSnap(
    float leftFrontX,
    float rightFrontX)
{
    auto const sweIndexStart = SWEBufferPrefixSize + ToSampleIndex(std::max(leftFrontX, -GameParameters::HalfMaxWorldWidth));
    auto const sweIndexEnd = SWEBufferPrefixSize + ToSampleIndex(std::min(rightFrontX, GameParameters::HalfMaxWorldWidth));

    float constexpr WaterDepression = 1.0f / SWEHeightFieldAmplification;

    for (auto idx = sweIndexStart; idx <= sweIndexEnd; ++idx)
        mSWEHeightField[idx] -= WaterDepression;
}

void OceanSurface::TriggerTsunami(float currentSimulationTime)
{
    // Choose X
    float const centerX = GameRandomEngine::GetInstance().GenerateUniformReal(
        -GameParameters::HalfMaxWorldWidth * 4.0f / 5.0f,
        GameParameters::HalfMaxWorldWidth * 4.0f / 5.0f);

    // Choose height
    float const tsunamiRelativeHeight = GameRandomEngine::GetInstance().GenerateUniformReal(MaxInteractiveWaveAbsRelativeHeight * 0.6f, MaxInteractiveWaveAbsRelativeHeight * 0.85f);

    // (Re-)start state machine
    mSWETsunamiWaveStateMachine.emplace(
        centerX,
        tsunamiRelativeHeight,
        0.0004f,
        currentSimulationTime);
}

void OceanSurface::TriggerRogueWave(
    float currentSimulationTime,
    Wind const & wind)
{
    // Choose locus
    float centerX;
    if (wind.GetBaseAndStormSpeedMagnitude() >= 0.0f)
    {
        // Left locus
        centerX = -GameParameters::HalfMaxWorldWidth;
    }
    else
    {
        // Right locus
        centerX = GameParameters::HalfMaxWorldWidth;
    }

    // Choose height
    float const rogueWaveRelativeHeight = GameRandomEngine::GetInstance().GenerateUniformReal(MaxInteractiveWaveAbsRelativeHeight * 0.2f, MaxInteractiveWaveAbsRelativeHeight * 0.4f);

    // (Re-)start state machine
    mSWERogueWaveWaveStateMachine.emplace(
        centerX,
        rogueWaveRelativeHeight,
        0.0005f,
        currentSimulationTime);
}

///////////////////////////////////////////////////////////////////////////////////////////////

template<OceanRenderDetailType DetailType>
void OceanSurface::InternalUpload(Render::RenderContext & renderContext) const
{
    static_assert(DetailType == OceanRenderDetailType::Basic || DetailType == OceanRenderDetailType::Detailed);

    register_int constexpr DetailXOffsetSamples = 2; // # of (whole) samples that the detailed planes are offset by

    float constexpr MidPlaneDamp = 0.8f;
    float constexpr BackPlaneDamp = 0.45f;

    //
    // We want to upload at most RenderSlices slices
    //

    // Find index of leftmost sample, and its corresponding world X
    auto const leftmostSampleIndex = FastTruncateToArchInt((renderContext.GetVisibleWorld().TopLeft.x + GameParameters::HalfMaxWorldWidth) / Dx);
    float sampleIndexWorldX = -GameParameters::HalfMaxWorldWidth + (Dx * leftmostSampleIndex);

    // Calculate number of samples required to cover screen from leftmost sample
    // up to the visible world right (included)
    float const coverageWorldWidth = renderContext.GetVisibleWorld().BottomRight.x - sampleIndexWorldX;
    auto const numberOfSamplesToRender = static_cast<size_t>(ceil(coverageWorldWidth / Dx));

    if (numberOfSamplesToRender >= RenderSlices<size_t>)
    {
        //
        // Zoom out from afar: each slice encompasses more than 1 sample;
        // we upload then RenderSlices slices, interpolating Y at each slice boundary
        //

        // Start uploading
        if constexpr (DetailType == OceanRenderDetailType::Basic)
            renderContext.UploadOceanBasicStart(RenderSlices<int>);
        else
            renderContext.UploadOceanDetailedStart(RenderSlices<int>);

        // Calculate dx between each pair of slices we want to upload
        float const sliceDx = coverageWorldWidth / RenderSlices<float>;

        if constexpr (DetailType == OceanRenderDetailType::Basic)
        {
            for (size_t s = 0;
                s <= RenderSlices<size_t>;
                ++s, sampleIndexWorldX = std::min(sampleIndexWorldX + sliceDx, GameParameters::HalfMaxWorldWidth))
            {
                renderContext.UploadOceanBasic(
                    sampleIndexWorldX,
                    GetHeightAt(sampleIndexWorldX));
            }
        }
        else
        {
            auto const getSampleAtX = [this](float sampleIndexWorldX)
            {
                //
                // Split sample index X into index in sample array and fractional part
                // between that sample and the next
                //

                assert(sampleIndexWorldX >= -GameParameters::HalfMaxWorldWidth
                    && sampleIndexWorldX <= GameParameters::HalfMaxWorldWidth + 1.0f); // Allow for compounding inaccuracies

                // Fractional index in the sample array
                float const sampleIndexF = (sampleIndexWorldX + GameParameters::HalfMaxWorldWidth) / Dx;

                // Integral part
                auto const sampleIndexI = FastTruncateToArchInt(sampleIndexF);

                // Fractional part within sample index and the next sample index
                float const sampleIndexDx = sampleIndexF - sampleIndexI;

                assert(sampleIndexI >= 0 && sampleIndexI <= static_cast<decltype(sampleIndexI)>(SamplesCount)); // Allow for compounding inaccuracies
                assert(sampleIndexDx >= 0.0f && sampleIndexDx < 1.0f);

                //
                // Interpolate sample at sampleIndexX
                //

                float const sample =
                    mSamples[sampleIndexI].SampleValue
                    + mSamples[sampleIndexI].SampleValuePlusOneMinusSampleValue * sampleIndexDx;

                return std::make_tuple(sample, sampleIndexI, sampleIndexDx);
            };

            // First step:
            //  - previous, current = s[0]
            auto [currentSample, currentSampleIndexI, currentSampleIndexDx] = getSampleAtX(sampleIndexWorldX);
            float previousDerivative = 0.0f; // [0] - [-1]
            float nextSample = 0.0f;

            for (size_t s = 0; s < RenderSlices<size_t>; ++s)
            {
                //
                // Interpolate back- and mid- samples at sampleIndeX minus offsets,
                // re-using the fractional part that we've already calculated for sampleIndexX
                //

                auto const indexBack = std::max(currentSampleIndexI - DetailXOffsetSamples * 2, register_int(0));
                float const sampleBack =
                    mSamples[indexBack].SampleValue
                    + mSamples[indexBack].SampleValuePlusOneMinusSampleValue * currentSampleIndexDx;

                auto const indexMid = std::max(currentSampleIndexI - DetailXOffsetSamples, register_int(0));
                float const sampleMid =
                    mSamples[indexMid].SampleValue
                    + mSamples[indexMid].SampleValuePlusOneMinusSampleValue * currentSampleIndexDx;

                // Get next sample
                float const nextSampleIndexWorldX = sampleIndexWorldX + sliceDx;
                std::tie(nextSample, currentSampleIndexI, currentSampleIndexDx) = getSampleAtX(nextSampleIndexWorldX);

                // Calculate second derivative
                float const nextDerivative = nextSample - currentSample;
                float const d2YFront = nextDerivative - previousDerivative;

                // Upload
                renderContext.UploadOceanDetailed(
                    sampleIndexWorldX,
                    sampleBack * BackPlaneDamp,
                    sampleMid * MidPlaneDamp,
                    currentSample,
                    d2YFront);

                // Advance
                currentSample = nextSample;
                previousDerivative = nextDerivative;
                sampleIndexWorldX = nextSampleIndexWorldX;
            }

            // We do one extra iteration as the number of slices is the number of quads, and the last vertical
            // quad side must be at the end of the width

            auto const indexBack = std::max(currentSampleIndexI - DetailXOffsetSamples * 2, register_int(0));
            float const sampleBack =
                mSamples[indexBack].SampleValue
                + mSamples[indexBack].SampleValuePlusOneMinusSampleValue * currentSampleIndexDx;

            auto const indexMid = std::max(currentSampleIndexI - DetailXOffsetSamples, register_int(0));
            float const sampleMid =
                mSamples[indexMid].SampleValue
                + mSamples[indexMid].SampleValuePlusOneMinusSampleValue * currentSampleIndexDx;

            renderContext.UploadOceanDetailed(
                sampleIndexWorldX,
                sampleBack * BackPlaneDamp,
                sampleMid * MidPlaneDamp,
                currentSample,
                -previousDerivative); // 0.0 - previousDerivative
        }
    }
    else
    {
        //
        // Zoom in: each sample encompasses multiple slices; we upload then just the
        // required number of samples - using straight, whole samples - which is less
        // than the max number of slices we're prepared to upload, and we let OpenGL
        // interpolate on our behalf
        //

        if constexpr (DetailType == OceanRenderDetailType::Basic)
            renderContext.UploadOceanBasicStart(numberOfSamplesToRender);
        else
            renderContext.UploadOceanDetailedStart(numberOfSamplesToRender);

        // We do one extra iteration as the number of slices is the number of quads, and the last vertical
        // quad side must be at the end of the width
        for (size_t s = 0; s <= numberOfSamplesToRender; ++s, sampleIndexWorldX += Dx)
        {
            if constexpr (DetailType == OceanRenderDetailType::Basic)
            {
                renderContext.UploadOceanBasic(
                    sampleIndexWorldX,
                    mSamples[leftmostSampleIndex + static_cast<register_int>(s)].SampleValue);
            }
            else
            {
                renderContext.UploadOceanDetailed(
                    sampleIndexWorldX,
                    mSamples[std::max(leftmostSampleIndex + static_cast<register_int>(s) - DetailXOffsetSamples * 2, register_int(0))].SampleValue * BackPlaneDamp,
                    mSamples[std::max(leftmostSampleIndex + static_cast<register_int>(s) - DetailXOffsetSamples, register_int(0))].SampleValue * MidPlaneDamp,
                    mSamples[leftmostSampleIndex + static_cast<register_int>(s)].SampleValue,
                    0.0f); // No need to worry with second derivative in zoom-in case
            }
        }
    }

    if constexpr (DetailType == OceanRenderDetailType::Basic)
        renderContext.UploadOceanBasicEnd();
    else
        renderContext.UploadOceanDetailedEnd();
}

void OceanSurface::RecalculateWaveCoefficients(
    Wind const & wind,
    GameParameters const & gameParameters)
{
    //
    // Basal waves
    //

    float baseWindSpeedMagnitude = std::abs(wind.GetBaseAndStormSpeedMagnitude()); // km/h
    if (baseWindSpeedMagnitude < 60)
        // y = 63.09401 - 63.09401*e^(-0.05025263*x)
        baseWindSpeedMagnitude = 63.09401f - 63.09401f * std::exp(-0.05025263f * baseWindSpeedMagnitude); // Dramatize

    float const baseWindSpeedSign = wind.GetBaseAndStormSpeedMagnitude() >= 0.0f ? 1.0f : -1.0f;

    // Amplitude
    // - Amplitude = f(WindSpeed, km/h), with f fitted over points from Full Developed Waves
    //   (H. V. Thurman, Introductory Oceanography, 1988)
    // y = 1.039702 - 0.08155357*x + 0.002481548*x^2

    float const basalWaveHeightBase = (baseWindSpeedMagnitude != 0.0f)
        ? 0.002481548f * (baseWindSpeedMagnitude * baseWindSpeedMagnitude)
          - 0.08155357f * baseWindSpeedMagnitude
          + 1.039702f
        : 0.0f;

    mBasalWaveAmplitude1 = basalWaveHeightBase / 2.0f * gameParameters.BasalWaveHeightAdjustment;
    mBasalWaveAmplitude2 = 0.75f * mBasalWaveAmplitude1;

    // Wavelength
    // - Wavelength = f(WaveHeight (adjusted), m), with f fitted over points from same table
    // y = -738512.1 + 738525.2*e^(+0.00001895026*x)

    float const basalWaveLengthBase =
        -738512.1f
        + 738525.2f * exp(0.00001895026f * (2.0f * mBasalWaveAmplitude1));

    float const basalWaveLength = basalWaveLengthBase * gameParameters.BasalWaveLengthAdjustment;

    assert(basalWaveLength != 0.0f);
    mBasalWaveNumber1 = baseWindSpeedSign * 2.0f * Pi<float> / basalWaveLength;
    mBasalWaveNumber2 = 0.66f * mBasalWaveNumber1;

    // Period
    // - Technically, period = sqrt(2 * Pi * L / g), however this doesn't fit the table, so:
    // - Period = f(WaveLength (adjusted), m), with f fitted over points from same table
    // y = 17.91851 - 15.52928*e^(-0.006572834*x)

    float const basalWavePeriodBase =
        17.91851f
        - 15.52928f * exp(-0.006572834f * basalWaveLength);

    assert(gameParameters.BasalWaveSpeedAdjustment != 0.0f);
    float const basalWavePeriod = basalWavePeriodBase / gameParameters.BasalWaveSpeedAdjustment;

    assert(basalWavePeriod != 0.0f);
    mBasalWaveAngularVelocity1 = 2.0f * Pi<float> / basalWavePeriod;
    mBasalWaveAngularVelocity2 = 0.75f * mBasalWaveAngularVelocity1;

    //
    // Pre-calculate basal wave sinusoid
    //
    // By pre-multiplying with the first basal wave's amplitude we may save
    // one multiplication later
    //

    mBasalWaveSin1.Recalculate(
        [a = mBasalWaveAmplitude1](float x)
        {
            return a * sin(2.0f * Pi<float> * x);
        });

    //
    // Store new parameter values that we are now current with
    //

    mWindBaseAndStormSpeedMagnitude = wind.GetBaseAndStormSpeedMagnitude();
    mBasalWaveHeightAdjustment = gameParameters.BasalWaveHeightAdjustment;
    mBasalWaveLengthAdjustment = gameParameters.BasalWaveLengthAdjustment;
    mBasalWaveSpeedAdjustment = gameParameters.BasalWaveSpeedAdjustment;
}

void OceanSurface::RecalculateAbnormalWaveTimestamps(GameParameters const & gameParameters)
{
    if (gameParameters.TsunamiRate.count() > 0)
    {
        mNextTsunamiTimestamp = CalculateNextAbnormalWaveTimestamp(
            mLastTsunamiTimestamp,
            gameParameters.TsunamiRate,
            TsunamiGracePeriod);
    }
    else
    {
        mNextTsunamiTimestamp = GameWallClock::time_point::max();
    }

    if (gameParameters.RogueWaveRate.count() > 0)
    {
        mNextRogueWaveTimestamp = CalculateNextAbnormalWaveTimestamp(
            mLastRogueWaveTimestamp,
            gameParameters.RogueWaveRate,
            RogueWaveGracePeriod);
    }
    else
    {
        mNextRogueWaveTimestamp = GameWallClock::time_point::max();
    }


    //
    // Store new parameter values that we are now current with
    //

    mTsunamiRate = gameParameters.TsunamiRate;
    mRogueWaveRate = gameParameters.RogueWaveRate;
}

template<typename TRateDuration, typename TGraceDuration>
GameWallClock::time_point OceanSurface::CalculateNextAbnormalWaveTimestamp(
    GameWallClock::time_point lastTimestamp,
    TRateDuration rate,
    TGraceDuration gracePeriod)
{
    float const rateSeconds = static_cast<float>(std::chrono::duration_cast<std::chrono::seconds>(rate).count());

    return lastTimestamp
        + gracePeriod
        + std::chrono::duration_cast<GameWallClock::duration>(
            std::chrono::duration<float>(
                GameRandomEngine::GetInstance().GenerateExponentialReal(1.0f / rateSeconds)));
}

/* Note: in this implementation we let go of the field advections,
   as they dont's seem to improve the simulation in any visible way.
void OceanSurface::AdvectFieldsTest()
{
    //
    // Semi-Lagrangian method
    //

    FixedSizeVector<float, SWETotalSamples + 1> newHeightField;

    // Process all height samples, except for boundary condition samples
    for (size_t i = SWEBoundaryConditionsSamples; i < SWETotalSamples - SWEBoundaryConditionsSamples; ++i)
    {
        // The height field values are at the center of the cell,
        // while velocities are at the edges - hence we need to take
        // the two neighboring velocities
        float const v = (mSWEVelocityField[i] + mSWEVelocityField[i + 1]) / 2.0f;

        // Calculate the (fractional) index that this height sample had one time step ago
        float const prevCellIndex =
            static_cast<float>(i)
            - v * GameParameters::SimulationStepTimeDuration<float> / Dx;

        // Transform index to ease interpolations, constraining the cell
        // to our grid at the same time
        float const prevCellIndex2 = std::min(
            std::max(0.0f, prevCellIndex),
            static_cast<float>(SWETotalSamples - 1));

        // Calculate integral and fractional parts of the index
        auto const prevCellIndexI = FastTruncateToArchInt(prevCellIndex2);
        float const prevCellIndexF = prevCellIndex2 - prevCellIndexI;
        assert(prevCellIndexF >= 0.0f && prevCellIndexF < 1.0f);

        // Set this height field sample as the previous (in time) sample,
        // interpolated between its two neighbors
        newHeightField[i] =
            (1.0f - prevCellIndexF) * mSWEHeightField[prevCellIndexI]
            + prevCellIndexF * mSWEHeightField[prevCellIndexI + 1];
    }

    std::memcpy(
        &(mSWEHeightField[SWEBoundaryConditionsSamples]),
        &(newHeightField[SWEBoundaryConditionsSamples]),
        SWETotalSamples - 2 * SWEBoundaryConditionsSamples);

    /////////////////////////////////////////////////////////////

    FixedSizeVector<float, SWETotalSamples + 1> newVelocityField;

    // Process all velocity samples, except for boundary condition samples
    //
    // Note: the last velocity sample is the one after the last height field sample
    for (size_t i = SWEBoundaryConditionsSamples; i <= SWETotalSamples - SWEBoundaryConditionsSamples; ++i)
    {
        // Velocity values are at the edges of the cell
        float const v = mSWEVelocityField[i];

        // Calculate the (fractional) index that this velocity sample had one time step ago
        float const prevCellIndex =
            static_cast<float>(i)
            - v * GameParameters::SimulationStepTimeDuration<float> / Dx;

        // Transform index to ease interpolations, constraining the cell
        // to our grid at the same time
        float const prevCellIndex2 = std::min(
            std::max(0.0f, prevCellIndex),
            static_cast<float>(SWETotalSamples - 1));

        // Calculate integral and fractional parts of the index
        auto const prevCellIndexI = FastTruncateToArchInt(prevCellIndex2);
        float const prevCellIndexF = prevCellIndex2 - prevCellIndexI;
        assert(prevCellIndexF >= 0.0f && prevCellIndexF < 1.0f);

        // Set this velocity field sample as the previous (in time) sample,
        // interpolated between its two neighbors
        newVelocityField[i] =
            (1.0f - prevCellIndexF) * mSWEVelocityField[prevCellIndexI]
            + prevCellIndexF * mSWEVelocityField[prevCellIndexI + 1];
    }

    std::memcpy(
        &(mSWEVelocityField[SWEBoundaryConditionsSamples]),
        &(newVelocityField[SWEBoundaryConditionsSamples]),
        SWETotalSamples - 2 * SWEBoundaryConditionsSamples);
}
*/

void OceanSurface::ImpartInteractiveWave(
    float x,
    float targetRelativeHeight,
    float growthRate,
    float worldRadius)
{
    //
    // Registers the will to adjust the SWE height field at the specified x to the specified height.
    // 
    // Widens the action field horizontally to mitigate the "cuspid problem".
    //
    // Notes on the "cuspid problem": the cuspid we see is the result of setting H and running two field cycles:
    //  - First, the H we set at x = X becomes Dt / Dx * (velocityField[i] - velocityField[i + 1]) smaller;
    //  - Then, for any target H, there are two "regime" H's:
    //      - The one at x = X - lower than H;
    //      - The one in the neighborhood, extending to infinite.
    //  - The cuspid itself is our interpolation! It's just that the regime H at x=X is way higher than the regime H at its neighboring cells
    // 

    auto const centerIndex = ToSampleIndex(x);
    float const targetAbsoluteHeight = targetRelativeHeight + SWEHeightFieldOffset;

    // Calculate radius: in general we want it linear with h so that it's MaxRadius at MaxAbsRelativeHeight, but 
    // we also want it to start at a certain value - H - at zero delta height. So we add a 1/h factor to the linear dependency.
    // The general formula is:
    //      calcd_radius = MaxRadius * height_fraction + alpha / (height_fraction + beta)
    // Imposing that this curve has a slope of zero at zero, we get that alpha = H^2 / MaxRadius and beta = H / MaxRadius
    float constexpr MaxRadius = 22.0f;
    float constexpr H = 3.0f;
    float constexpr alpha = H * H / MaxRadius;
    float constexpr beta = H / MaxRadius;
    float const heightFraction = std::abs(targetRelativeHeight) / MaxInteractiveWaveAbsRelativeHeight;
    float const actionRadius = std::max(
        MaxRadius * heightFraction + alpha / (heightFraction + beta),
        worldRadius); // Take into account also the interactive radius

    // Set at central
    mInteractiveWaveTargetHeight[centerIndex] = targetAbsoluteHeight;
    mInteractiveWaveTargetHeightGrowthCoefficient[centerIndex] = 1.0f;
    mInteractiveWaveHeightGrowthCoefficientGrowthRate[centerIndex] = growthRate;

    // Set around
    for (int64_t d = 1; d <= static_cast<int64_t>(std::floor(actionRadius)); ++d)
    {
        float const coeff =
            1.0f - (static_cast<float>(d) / actionRadius) * (static_cast<float>(d) / actionRadius);

        if (centerIndex - d >= 0)
        {
            mInteractiveWaveTargetHeight[centerIndex - d] = targetAbsoluteHeight;
            mInteractiveWaveTargetHeightGrowthCoefficient[centerIndex - d] = coeff;
            mInteractiveWaveHeightGrowthCoefficientGrowthRate[centerIndex - d] = growthRate;
        }

        if (centerIndex + d < SamplesCount)
        {
            mInteractiveWaveTargetHeight[centerIndex + d] = targetAbsoluteHeight;
            mInteractiveWaveTargetHeightGrowthCoefficient[centerIndex + d] = coeff;
            mInteractiveWaveHeightGrowthCoefficientGrowthRate[centerIndex + d] = growthRate;
        }
    }
}

void OceanSurface::UpdateInteractiveWaves()
{
    float * const restrict currentHeightGrowthCoefficientBuffer = mInteractiveWaveCurrentHeightGrowthCoefficient.data();
    float * const restrict sweHeightFieldBuffer = mSWEHeightField.data() + SWEBufferPrefixSize;

    for (size_t i = 0; i < SamplesCount; ++i)
    {
        // Update growth coefficient
        currentHeightGrowthCoefficientBuffer[i] +=
            (mInteractiveWaveTargetHeightGrowthCoefficient[i] - currentHeightGrowthCoefficientBuffer[i])
            * mInteractiveWaveHeightGrowthCoefficientGrowthRate[i];

        // Smooth current height to target according to current growth coefficient
        sweHeightFieldBuffer[i] +=
            (mInteractiveWaveTargetHeight[i] - sweHeightFieldBuffer[i])
            * currentHeightGrowthCoefficientBuffer[i];
    }
}

void OceanSurface::ResetInteractiveWaves()
{
    mInteractiveWaveTargetHeightGrowthCoefficient.fill(0.0f);
    mInteractiveWaveHeightGrowthCoefficientGrowthRate.fill(0.1f); // Magic number: rate with which we stop pinning the SWE height field
}

void OceanSurface::SmoothDeltaBufferIntoHeightField()
{
    //
    // Incorporate delta-height into height field, after smoothing
    //
    // We use a two-pass average on a window of width DeltaHeightSmoothing,
    // centered on the sample
    //

    Algorithms::SmoothBufferAndAdd<SamplesCount, DeltaHeightSmoothing>(
        mDeltaHeightBuffer.data() + DeltaHeightBufferPrefixSize,
        mSWEHeightField.data() + SWEBufferPrefixSize);

    // Clear delta-height buffer
    mDeltaHeightBuffer.fill(0.0f);
}

void OceanSurface::ApplyDampingBoundaryConditions()
{
    for (size_t i = 0; i < SWEBoundaryConditionsSamples; ++i)
    {
        float const damping = static_cast<float>(i) / static_cast<float>(SWEBoundaryConditionsSamples);

        // Left side

        mSWEHeightField[SWEBufferAlignmentPrefixSize + i] =
            (mSWEHeightField[SWEBufferAlignmentPrefixSize + i] - SWEHeightFieldOffset) * damping
            + SWEHeightFieldOffset;

        mSWEVelocityField[SWEBufferAlignmentPrefixSize + i] *= damping;

        // Right side

        mSWEHeightField[SWEBufferAlignmentPrefixSize + SWEBoundaryConditionsSamples + SamplesCount + SWEBoundaryConditionsSamples - 1 - i] =
            (mSWEHeightField[SWEBufferAlignmentPrefixSize + SWEBoundaryConditionsSamples + SamplesCount + SWEBoundaryConditionsSamples - 1 - i] - SWEHeightFieldOffset) * damping
            + SWEHeightFieldOffset;

        // For symmetry we actually damp the v-sample that is *after* this h-sample
        mSWEVelocityField[SWEBufferAlignmentPrefixSize + SWEBoundaryConditionsSamples + SamplesCount + SWEBoundaryConditionsSamples - 1 - i + 1] *= damping;
    }
}

void OceanSurface::UpdateFields(GameParameters const & gameParameters)
{
    //
    // SWE Update
    //
    // "q‐Upwind Numerical Scheme" from "Improving the stability of a simple formulation of the shallow water equations for 2‐D flood modeling",
    //      de Almeida, Bates, Freer, Souvignet (2012), https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2011WR011570
    //
    // Height field  : from 0 to SWETotalSamples
    // Velocity field: from 1 to SWETotalSamples (i.e. at boundaries it's inner only)
    //                 H[i] has V[i] at its left and V[i+1] at its right
    //

    float constexpr G = GameParameters::GravityMagnitude;
    float constexpr Dt = GameParameters::SimulationStepTimeDuration<float>;
    float const previousVWeight1 = 1.0f - gameParameters.WaveSmoothnessAdjustment;
    float const previousVWeight2 = gameParameters.WaveSmoothnessAdjustment / 2.0f; // Includes /2 for average

    float * const restrict heightField = mSWEHeightField.data() + SWEBufferAlignmentPrefixSize;
    float * const restrict velocityField = mSWEVelocityField.data() + SWEBufferAlignmentPrefixSize;

    // Update first height field value
    heightField[0] *=
        1.0f + Dt / Dx * (velocityField[0] - velocityField[0 + 1]);

    for (size_t i = 1; i < SWEBoundaryConditionsSamples + SamplesCount + SWEBoundaryConditionsSamples; ++i)
    {
        // Update height field
        heightField[i] *=
            1.0f + Dt / Dx * (velocityField[i] - velocityField[i + 1]);

        // V @ t-1: mix of V[i] and of avg(V[i-1], V[i+1])
        float const previousV =
            previousVWeight1 * velocityField[i]
            + previousVWeight2 * (velocityField[i - 1] + velocityField[i + 1]);

        // Update velocity field
        velocityField[i] = previousV - G * Dt / Dx * (heightField[i] - heightField[i - 1]);
    }
}

void OceanSurface::GenerateSamples(
    float currentSimulationTime,
    Wind const & wind,
    GameParameters const & /*gameParameters*/)
{
    //
    // Sample values are a combination of:
    //  - SWE's height field
    //  - Basal waves
    //  - Wind gust ripples
    //

    // Secondary basal component
    float const secondaryBasalComponentPhase = Pi<float> * sin(currentSimulationTime);

    //
    // Wind gust ripples
    //

    float constexpr WindRippleWaveNumber = 2.0f; // # waves per unit of length
    float constexpr WindRippleWaveHeight = 0.125f;

    float const windSpeedAbsoluteMagnitude = wind.GetCurrentWindSpeed().length();
    float const windSpeedGustRelativeAmplitude = wind.GetMaxSpeedMagnitude() - wind.GetBaseAndStormSpeedMagnitude();
    float const rawWindNormalizedIncisiveness = (windSpeedGustRelativeAmplitude == 0.0f)
        ? 0.0f
        : std::max(0.0f, windSpeedAbsoluteMagnitude - std::abs(wind.GetBaseAndStormSpeedMagnitude()))
        / std::abs(windSpeedGustRelativeAmplitude);

    float const windRipplesAngularVelocity = (wind.GetBaseAndStormSpeedMagnitude() >= 0)
        ? 128.0f
        : -128.0f;

    float const smoothedWindNormalizedIncisiveness = mWindIncisivenessRunningAverage.Update(rawWindNormalizedIncisiveness);
    float const windRipplesWaveHeight = WindRippleWaveHeight * smoothedWindNormalizedIncisiveness;


    //
    // Generate samples
    //

    float const x = -GameParameters::HalfMaxWorldWidth;

    float const basalWave2AmplitudeCoeff =
        (mBasalWaveAmplitude1 != 0.0f)
        ? mBasalWaveAmplitude2 / mBasalWaveAmplitude1
        : 0.0f;

    float const rippleWaveAmplitudeCoeff =
        (mBasalWaveAmplitude1 != 0.0f)
        ? windRipplesWaveHeight / mBasalWaveAmplitude1
        : 0.0f;

    float sinArg1 = (mBasalWaveNumber1 * x - mBasalWaveAngularVelocity1 * currentSimulationTime) / (2 * Pi<float>);
    float sinArg2 = (mBasalWaveNumber2 * x - mBasalWaveAngularVelocity2 * currentSimulationTime + secondaryBasalComponentPhase) / (2 * Pi<float>);
    float sinArgRipple = (WindRippleWaveNumber * x - windRipplesAngularVelocity * currentSimulationTime) / (2 * Pi<float>);

    // sample index = 0
    float previousSampleValue;
    {
        float const sweValue =
            (mSWEHeightField[SWEBufferPrefixSize + 0] - SWEHeightFieldOffset)
            * SWEHeightFieldAmplification;

        float const basalValue1 =
            mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg1);

        float const basalValue2 =
            basalWave2AmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg2);

        float const rippleValue =
            rippleWaveAmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArgRipple);

        previousSampleValue =
            sweValue
            + basalValue1
            + basalValue2
            + rippleValue;

        mSamples[0].SampleValue = previousSampleValue;
    }

    float const sinArg1Dx = mBasalWaveNumber1 * Dx / (2 * Pi<float>);
    float const sinArg2Dx = mBasalWaveNumber2 * Dx / (2 * Pi<float>);
    float const sinArgRippleDx = WindRippleWaveNumber * Dx / (2 * Pi<float>);

    // sample index = 1...SamplesCount - 1
    for (size_t i = 1; i < SamplesCount; ++i)
    {
        float const sweValue =
            (mSWEHeightField[SWEBufferPrefixSize + i] - SWEHeightFieldOffset)
            * SWEHeightFieldAmplification;

        sinArg1 += sinArg1Dx;
        float const basalValue1 =
            mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg1);

        sinArg2 += sinArg2Dx;
        float const basalValue2 =
            basalWave2AmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg2);

        sinArgRipple += sinArgRippleDx;
        float const rippleValue =
            rippleWaveAmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArgRipple);

        float const sampleValue =
            sweValue
            + basalValue1
            + basalValue2
            + rippleValue;

        mSamples[i].SampleValue = sampleValue;
        mSamples[i - 1].SampleValuePlusOneMinusSampleValue = sampleValue - previousSampleValue;

        previousSampleValue = sampleValue;
    }

    assert(mSamples[SamplesCount - 1].SampleValuePlusOneMinusSampleValue == 0.0f); // From cctor

    // Populate extra sample - same value as last sample
    assert(previousSampleValue == mSamples[SamplesCount - 1].SampleValue);
    mSamples[SamplesCount].SampleValue = previousSampleValue;

    assert(mSamples[SamplesCount].SampleValuePlusOneMinusSampleValue == 0.0f); // From cctor
}

}