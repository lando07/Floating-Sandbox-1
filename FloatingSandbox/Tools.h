/***************************************************************************************
* Original Author:      Gabriele Giuseppini
* Created:              2018-06-17
* Copyright:            Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "SoundController.h"

#include <Game/IGameController.h>
#include <Game/ResourceLocator.h>

#include <GameCore/GameRandomEngine.h>
#include <GameCore/GameTypes.h>

#include <wx/image.h>
#include <wx/window.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

struct IToolCursorManager
{
    virtual void SetToolCursor(wxImage const & basisImage, float strength = 0.0f) = 0;
};

enum class ToolType
{
    Move = 0,
    MoveAll,
    PickAndPull,
    Smash,
    Saw,
    HeatBlaster,
    FireExtinguisher,
    Grab,
    Swirl,
    Pin,
    InjectPressure,
    FloodHose,
    AntiMatterBomb,
    ImpactBomb,
    RCBomb,
    TimerBomb,
    WaveMaker,
    TerrainAdjust,
    Scrub,
    RepairStructure,
    ThanosSnap,
    ScareFish,
    PhysicsProbe,
    BlastTool,
    ElectricSparkTool,
    WindMakerTool,
    LaserCannonTool
};

struct InputState
{
    bool IsLeftMouseDown;
    bool IsRightMouseDown;
    bool IsShiftKeyDown;
    DisplayLogicalCoordinates MousePosition;
    DisplayLogicalCoordinates PreviousMousePosition;

    InputState()
        : IsLeftMouseDown(false)
        , IsRightMouseDown(false)
        , IsShiftKeyDown(false)
        , MousePosition(0, 0)
        , PreviousMousePosition(0, 0)
    {
    }
};

/*
 * Base abstract class of all tools.
 */
class Tool
{
public:

    virtual ~Tool() {}

    ToolType GetToolType() const { return mToolType; }

    virtual void Initialize(InputState const & inputState) = 0;
    virtual void Deinitialize() = 0;

    virtual void UpdateSimulation(InputState const & inputState, float currentSimulationTime) = 0;

    virtual void OnMouseMove(InputState const & inputState) = 0;
    virtual void OnLeftMouseDown(InputState const & inputState) = 0;
    virtual void OnLeftMouseUp(InputState const & inputState) = 0;
    virtual void OnShiftKeyDown(InputState const & inputState) = 0;
    virtual void OnShiftKeyUp(InputState const & inputState) = 0;

protected:

    Tool(
        ToolType toolType,
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController)
        : mToolCursorManager(toolCursorManager)
        , mGameController(gameController)
        , mSoundController(soundController)
        , mToolType(toolType)
    {}

    IToolCursorManager & mToolCursorManager;
    IGameController & mGameController;
    SoundController & mSoundController;

private:

    ToolType const mToolType;
};

/*
 * Base class of tools that shoot once.
 */
class OneShotTool : public Tool
{
public:

    virtual ~OneShotTool() {}

    virtual void Deinitialize() override {}

    virtual void UpdateSimulation(InputState const & /*inputState*/, float /*currentSimulationTime*/) override {}

    virtual void OnMouseMove(InputState const & /*inputState*/) override {}
    virtual void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    virtual void OnShiftKeyUp(InputState const & /*inputState*/) override {}

protected:

    OneShotTool(
        ToolType toolType,
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController)
        : Tool(
            toolType,
            toolCursorManager,
            gameController,
            soundController)
    {}
};

/*
 * Base class of tools that apply continuously.
 */
class ContinuousTool : public Tool
{
public:

    virtual ~ContinuousTool() {}

    virtual void Initialize(InputState const & inputState) override
    {
        // Initialize the continuous tool state
        mPreviousMousePosition = inputState.MousePosition;
        mPreviousTimestamp = std::chrono::steady_clock::now();
        mCumulatedTime = std::chrono::microseconds(0);

        // Initialize the cursor state
        mLastCursor = wxImage();
        mLastQuantizedCursorStrength.reset();
    }

    virtual void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override;

    virtual void OnMouseMove(InputState const & /*inputState*/) override {}

    virtual void OnLeftMouseDown(InputState const & inputState) override
    {
        // Initialize the continuous tool state
        mPreviousMousePosition = inputState.MousePosition;
        mPreviousTimestamp = std::chrono::steady_clock::now();
        mCumulatedTime = std::chrono::microseconds(0);
    }

    virtual void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    virtual void OnShiftKeyUp(InputState const & /*inputState*/) override {}

protected:

    ContinuousTool(
        ToolType toolType,
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController)
        : Tool(
            toolType,
            toolCursorManager,
            gameController,
            soundController)
        , mPreviousMousePosition(0, 0)
        , mPreviousTimestamp(std::chrono::steady_clock::now())
        , mCumulatedTime(0)
        , mLastCursor()
        , mLastQuantizedCursorStrength()
    {}

    virtual void ApplyTool(
        std::chrono::microseconds const & cumulatedTime,
        InputState const & inputState) = 0;

    void InternalSetToolCursor(wxImage const & basisImage, float strength)
    {
        // Suppresss redundant calls to change cursor

        int const quantizedCursorStrength = static_cast<int>(strength * 64.0f);

        if (!mLastCursor.IsSameAs(basisImage)
            || !mLastQuantizedCursorStrength.has_value()
            || *mLastQuantizedCursorStrength != quantizedCursorStrength)
        {
            mToolCursorManager.SetToolCursor(basisImage, strength);

            // Remember current cursor
            mLastCursor = basisImage;
            mLastQuantizedCursorStrength = quantizedCursorStrength;
        }
    }

private:

    //
    // Tool state
    //

    // Previous mouse position and time when we looked at it
    DisplayLogicalCoordinates mPreviousMousePosition;
    std::chrono::steady_clock::time_point mPreviousTimestamp;

    // The total accumulated press time - the proxy for the strength of the tool
    std::chrono::microseconds mCumulatedTime;

    // The last cursor (image and strength) which we've set; used to suppress
    // redundant, identical calls to SetCursor
    wxImage mLastCursor;
    std::optional<int> mLastQuantizedCursorStrength;
};

//////////////////////////////////////////////////////////////////////////////////////////
// Tools
//////////////////////////////////////////////////////////////////////////////////////////

template <typename TMovableObjectId>
class BaseMoveTool : public OneShotTool
{
public:

    virtual void Initialize(InputState const & inputState) override
    {
        // Reset state
        mEngagedMovableObjectId.reset();
        mCurrentTrajectory.reset();
        mRotationCenter.reset();

        // Initialize state
        ProcessInputStateChange(inputState);
    }

    virtual void Deinitialize() override
    {
        if (!!mEngagedMovableObjectId)
        {
            // Tell IGameController to stop moving
            mGameController.SetMoveToolEngaged(false);
        }
    }

    virtual void UpdateSimulation(InputState const & /*inputState*/, float /*currentSimulationTime*/) override
    {
        if (mCurrentTrajectory.has_value())
        {
            //
            // We're following a trajectory
            //

            auto const now = std::chrono::steady_clock::now();

            //
            // Smooth current position
            //

            float const rawProgress =
                static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(now - mCurrentTrajectory->StartTimestamp).count())
                / static_cast<float>(TrajectoryLag.count());

            // ((x+0.7)^2-0.49)/2.0
            float const progress = (pow(std::min(rawProgress, 1.0f) + 0.7f, 2.0f) - 0.49f) / 2.0f;

            vec2f const newCurrentPosition =
                mCurrentTrajectory->StartPosition
                + (mCurrentTrajectory->EndPosition - mCurrentTrajectory->StartPosition) * progress;

            // Tell IGameController
            if (!mCurrentTrajectory->RotationCenter)
            {
                // Calculate offset
                vec2f const fractionalOffset = newCurrentPosition - mCurrentTrajectory->CurrentPosition + mCurrentTrajectory->CumulativeUnconsumedMoveOffset;
                DisplayLogicalSize const integralOffset = DisplayLogicalSize::FromFloatRound(fractionalOffset);

                // Move
                mGameController.MoveBy(
                    mCurrentTrajectory->EngagedMovableObjectId,
                    integralOffset,
                    integralOffset);

                // Accumulate debt
                mCurrentTrajectory->CumulativeUnconsumedMoveOffset = fractionalOffset - integralOffset.ToFloat();
            }
            else
            {
                // Rotate
                mGameController.RotateBy(
                    mCurrentTrajectory->EngagedMovableObjectId,
                    newCurrentPosition.y - mCurrentTrajectory->CurrentPosition.y,
                    DisplayLogicalCoordinates::FromFloatRound(*mCurrentTrajectory->RotationCenter),
                    newCurrentPosition.y - mCurrentTrajectory->CurrentPosition.y);
            }

            // Store new current position
            mCurrentTrajectory->CurrentPosition = newCurrentPosition;

            // Check whether we are done
            if (rawProgress >= 1.0f)
            {
                //
                // Close trajectory
                //

                if (!!mEngagedMovableObjectId)
                {
                    // Tell game controller to stop inertia
                    if (!mCurrentTrajectory->RotationCenter)
                    {
                        // Move to stop
                        mGameController.MoveBy(
                            mCurrentTrajectory->EngagedMovableObjectId,
                            DisplayLogicalSize(0, 0),
                            DisplayLogicalSize(0, 0));
                    }
                    else
                    {
                        // Rotate to stop
                        mGameController.RotateBy(
                            mCurrentTrajectory->EngagedMovableObjectId,
                            0.0f,
                            DisplayLogicalCoordinates::FromFloatRound(*mCurrentTrajectory->RotationCenter),
                            0.0f);
                    }
                }

                // Reset trajectory
                mCurrentTrajectory.reset();
            }
        }
    }

    virtual void OnMouseMove(InputState const & inputState) override
    {
        if (mEngagedMovableObjectId.has_value())
        {
            auto const now = std::chrono::steady_clock::now();

            if (mCurrentTrajectory.has_value())
            {
                //
                // We already have a trajectory
                //

                // Re-register the start position from where we are now
                mCurrentTrajectory->StartPosition = mCurrentTrajectory->CurrentPosition;

                // If we're enough into the trajectory, restart the timing - to avoid cramming a mouse move stretch
                // into a tiny little time interval
                if (now > mCurrentTrajectory->StartTimestamp + TrajectoryLag / 2)
                {
                    mCurrentTrajectory->StartTimestamp = now;
                    mCurrentTrajectory->EndTimestamp = mCurrentTrajectory->StartTimestamp + TrajectoryLag;
                }
            }
            else
            {
                //
                // Start a new trajectory
                //

                mCurrentTrajectory = Trajectory(*mEngagedMovableObjectId);
                mCurrentTrajectory->RotationCenter = mRotationCenter;
                mCurrentTrajectory->StartPosition = inputState.PreviousMousePosition.ToFloat();
                mCurrentTrajectory->CurrentPosition = mCurrentTrajectory->StartPosition;
                mCurrentTrajectory->CumulativeUnconsumedMoveOffset = vec2f::zero();
                mCurrentTrajectory->StartTimestamp = now;
                mCurrentTrajectory->EndTimestamp = mCurrentTrajectory->StartTimestamp + TrajectoryLag;
            }

            mCurrentTrajectory->EndPosition = inputState.MousePosition.ToFloat();
        }
    }

    virtual void OnLeftMouseDown(InputState const & inputState) override
    {
        ProcessInputStateChange(inputState);
    }

    virtual void OnLeftMouseUp(InputState const & inputState) override
    {
        ProcessInputStateChange(inputState);
    }

    virtual void OnShiftKeyDown(InputState const & inputState) override
    {
        ProcessInputStateChange(inputState);
    }

    virtual void OnShiftKeyUp(InputState const & inputState) override
    {
        ProcessInputStateChange(inputState);
    }

protected:

    BaseMoveTool(
        ToolType toolType,
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        wxImage upCursorImage,
        wxImage downCursorImage,
        wxImage rotateUpCursorImage,
        wxImage rotateDownCursorImage)
        : OneShotTool(
            toolType,
            toolCursorManager,
            gameController,
            soundController)
        , mEngagedMovableObjectId()
        , mCurrentTrajectory()
        , mRotationCenter()
        , mUpCursorImage(upCursorImage)
        , mDownCursorImage(downCursorImage)
        , mRotateUpCursorImage(rotateUpCursorImage)
        , mRotateDownCursorImage(rotateDownCursorImage)
    {
    }

private:

    void ProcessInputStateChange(InputState const & inputState)
    {
        //
        // Update state
        //

        if (inputState.IsLeftMouseDown)
        {
            // Left mouse down

            if (!mEngagedMovableObjectId.has_value())
            {
                //
                // We're currently not engaged
                //

                // Check with game controller
                mGameController.PickObjectToMove(
                    inputState.MousePosition,
                    mEngagedMovableObjectId);

                if (mEngagedMovableObjectId.has_value())
                {
                    // Engaged!

                    // Tell IGameController
                    mGameController.SetMoveToolEngaged(true);
                }
            }
        }
        else
        {
            // Left mouse up

            if (mEngagedMovableObjectId.has_value())
            {
                //
                // We're currently engaged
                //

                // Disengage
                mEngagedMovableObjectId.reset();

                // Reset rotation
                mRotationCenter.reset();

                if (mCurrentTrajectory.has_value())
                {
                    //
                    // We are in the midst of a trajectory
                    //

                    // Impart last inertia == distance left
                    if (!mCurrentTrajectory->RotationCenter)
                    {
                        mGameController.MoveBy(
                            mCurrentTrajectory->EngagedMovableObjectId,
                            DisplayLogicalSize(0, 0),
                            DisplayLogicalSize::FromFloatRound(mCurrentTrajectory->EndPosition - mCurrentTrajectory->CurrentPosition));
                    }
                    else
                    {
                        // Rotate to stop
                        mGameController.RotateBy(
                            mCurrentTrajectory->EngagedMovableObjectId,
                            0.0f,
                            DisplayLogicalCoordinates::FromFloatRound(*mCurrentTrajectory->RotationCenter),
                            mCurrentTrajectory->EndPosition.y - mCurrentTrajectory->CurrentPosition.y);
                    }

                    // Stop trajectory
                    mCurrentTrajectory.reset();
                }

                // Tell IGameController we've stopped moving
                mGameController.SetMoveToolEngaged(false);
            }
        }

        if (inputState.IsShiftKeyDown)
        {
            // Shift key down

            if (!mRotationCenter && mEngagedMovableObjectId.has_value())
            {
                //
                // We're engaged and not in rotation mode yet
                //

                assert(inputState.IsLeftMouseDown);

                // Start rotation mode
                mRotationCenter = inputState.MousePosition.ToFloat();

                // Reset current trajectory, if any
                mCurrentTrajectory.reset();
            }
        }
        else
        {
            // Shift key up

            if (!!mRotationCenter)
            {
                //
                // We are in rotation mode
                //

                // Stop rotation mode
                mRotationCenter.reset();

                // Reset current trajectory, if any
                mCurrentTrajectory.reset();
            }
        }

        //
        // Update cursor
        //

        if (!mEngagedMovableObjectId)
        {
            if (!mRotationCenter)
            {
                mToolCursorManager.SetToolCursor(mUpCursorImage);
            }
            else
            {
                mToolCursorManager.SetToolCursor(mRotateUpCursorImage);
            }
        }
        else
        {
            if (!mRotationCenter)
            {
                mToolCursorManager.SetToolCursor(mDownCursorImage);
            }
            else
            {
                mToolCursorManager.SetToolCursor(mRotateDownCursorImage);
            }
        }
    }

private:

    // When engaged, the ID of the element (point of a ship) we're currently moving
    std::optional<TMovableObjectId> mEngagedMovableObjectId;

    struct Trajectory
    {
        TMovableObjectId EngagedMovableObjectId;
        std::optional<vec2f> RotationCenter;

        vec2f StartPosition;
        vec2f CurrentPosition;
        vec2f EndPosition;

        vec2f CumulativeUnconsumedMoveOffset; // We only offset by integral steps, and here we cumulate our rounding debt

        std::chrono::steady_clock::time_point StartTimestamp;
        std::chrono::steady_clock::time_point EndTimestamp;

        Trajectory(TMovableObjectId engagedMovableObjectId)
            : EngagedMovableObjectId(engagedMovableObjectId)
        {}
    };

    static constexpr std::chrono::milliseconds TrajectoryLag = std::chrono::milliseconds(500);

    // When set, we're smoothing the mouse position along a trajectory
    std::optional<Trajectory> mCurrentTrajectory;

    // When set, we're rotating
    std::optional<vec2f> mRotationCenter;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
    wxImage const mRotateUpCursorImage;
    wxImage const mRotateDownCursorImage;
};

class MoveTool final : public BaseMoveTool<ElementId>
{
public:

    MoveTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);
};

class MoveAllTool final : public BaseMoveTool<ShipId>
{
public:

    MoveAllTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);
};

class PickAndPullTool final : public Tool
{
public:

    PickAndPullTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        mCurrentEngagementState.reset();

        // Set cursor
        SetCurrentCursor();
    }

    void Deinitialize() override {}

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        bool wasEngaged = !!mCurrentEngagementState;

        if (inputState.IsLeftMouseDown)
        {
            if (!mCurrentEngagementState)
            {
                //
                // Not engaged...
                // ...see if we're able to pick a point and thus start engagement
                //

                auto elementId = mGameController.PickObjectForPickAndPull(inputState.MousePosition);
                if (elementId.has_value())
                {
                    //
                    // Engage!
                    //

                    mCurrentEngagementState.emplace(
                        *elementId,
                        inputState.MousePosition.ToFloat());

                    // Play sound
                    mSoundController.PlayPliersSound(mGameController.IsUnderwater(*elementId));
                }
            }
            else
            {
                //
                // Engaged
                //

                // 1. Update target position
                mCurrentEngagementState->TargetScreenPosition = inputState.MousePosition.ToFloat();

                // 2. Calculate convergence speed based on speed of mouse move

                vec2f const mouseMovementStride = inputState.MousePosition.ToFloat() - mCurrentEngagementState->LastScreenPosition;
                float const worldStride = mGameController.ScreenOffsetToWorldOffset(DisplayLogicalSize::FromFloatRound(mouseMovementStride)).length();

                // New convergence rate:
                // - Stride < 2.0: 0.03 (76 steps to 0.9 of target)
                // - Stride > 50.0: 0.09 (<20 steps to 0.9 of target)
                float constexpr MinConvRate = 0.03f;
                float constexpr MaxConvRate = 0.09f;
                float const newConvergenceSpeed =
                    MinConvRate
                    + (MaxConvRate - MinConvRate) * SmoothStep(2.0f, 50.0f, worldStride);

                // Change current convergence rate depending on how much mouse has moved
                // - Small mouse movement: old speed
                // - Large mouse movement: new speed
                float newSpeedAlpha = SmoothStep(0.0f, 3.0, mouseMovementStride.length());
                mCurrentEngagementState->CurrentConvergenceSpeed = Mix(
                    mCurrentEngagementState->CurrentConvergenceSpeed,
                    newConvergenceSpeed,
                    newSpeedAlpha);

                // Update last mouse position
                mCurrentEngagementState->LastScreenPosition = inputState.MousePosition.ToFloat();

                // 3. Converge towards target position
                mCurrentEngagementState->CurrentScreenPosition +=
                    (mCurrentEngagementState->TargetScreenPosition - mCurrentEngagementState->CurrentScreenPosition)
                    * mCurrentEngagementState->CurrentConvergenceSpeed;

                // 4. Apply force towards current position
                mGameController.Pull(
                    mCurrentEngagementState->PickedParticle,
                    DisplayLogicalCoordinates::FromFloatRound(mCurrentEngagementState->CurrentScreenPosition));
            }
        }
        else
        {
            // Disengage (in case we're engaged)
            mCurrentEngagementState.reset();
        }

        if (!!mCurrentEngagementState != wasEngaged)
        {
            // State change

            // Update cursor
            SetCurrentCursor();
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        mToolCursorManager.SetToolCursor(!!mCurrentEngagementState ? mDownCursorImage : mUpCursorImage);
    }

    // Our state

    struct EngagementState
    {
        ElementId PickedParticle;
        vec2f CurrentScreenPosition;
        vec2f TargetScreenPosition;
        vec2f LastScreenPosition;
        float CurrentConvergenceSpeed;

        EngagementState(
            ElementId pickedParticle,
            vec2f startScreenPosition)
            : PickedParticle(pickedParticle)
            , CurrentScreenPosition(startScreenPosition)
            , TargetScreenPosition(startScreenPosition)
            , LastScreenPosition(startScreenPosition)
            , CurrentConvergenceSpeed(0.03f)
        {}
    };

    std::optional<EngagementState> mCurrentEngagementState; // When set, indicates it's engaged

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};

class SmashTool final : public ContinuousTool
{
public:

    SmashTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        ContinuousTool::Initialize(inputState);

        // Reset cursor
        SetBasisCursor(inputState);
    }

    void Deinitialize() override {}

    void OnLeftMouseDown(InputState const & inputState) override
    {
        ContinuousTool::OnLeftMouseDown(inputState);

        // Reset cursor
        SetBasisCursor(inputState);
    }

    void OnLeftMouseUp(InputState const & inputState) override
    {
        // Reset cursor
        SetBasisCursor(inputState);
    }

protected:

    void ApplyTool(
        std::chrono::microseconds const & cumulatedTime,
        InputState const & inputState) override;

private:

    void SetBasisCursor(InputState const & inputState)
    {
        if (inputState.IsLeftMouseDown)
        {
            // Down
            mToolCursorManager.SetToolCursor(mDownCursorImage, 0.0f);
        }
        else
        {
            // Up
            mToolCursorManager.SetToolCursor(mUpCursorImage, 0.0f);
        }
    }

    wxImage mUpCursorImage;
    wxImage mDownCursorImage;
};

class SawTool final : public Tool
{
public:

    SawTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        if (inputState.IsLeftMouseDown)
        {
            InitializeDown(inputState);
        }

        // Set cursor
        SetCurrentCursor(inputState);
    }

    void Deinitialize() override
    {
        // Stop sound if we're playing
        mSoundController.StopSawSound();
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        if (inputState.IsLeftMouseDown)
        {
            // State is initialized
            assert(mPreviousMousePos.has_value());

            // Update underwater-ness of sound
            bool isUnderwater = mGameController.IsUnderwater(inputState.MousePosition);
            if (isUnderwater != mIsUnderwater)
            {
                // Update sound
                mSoundController.PlaySawSound(isUnderwater);

                // Update state
                mIsUnderwater = isUnderwater;
            }

            // Update down cursor
            ++mDownCursorCounter;
            SetCurrentCursor(inputState);
        }
    }

    void OnMouseMove(InputState const & inputState) override
    {
        if (inputState.IsLeftMouseDown)
        {
            // State is initialized
            assert(mPreviousMousePos.has_value());

            // Calculate target position depending on whether we're locked or not
            auto const targetPosition = CalculateTargetPosition(inputState);

            // Saw
            mGameController.SawThrough(
                *mPreviousMousePos,
                targetPosition,
                mIsFirstSegment);

            // Not anymore the first segment
            mIsFirstSegment = false;

            // Check whether we need to lock or unlock the direction
            if (inputState.IsShiftKeyDown
                && !mCurrentLockedDirection.has_value())
            {
                vec2f const lastDirection = (targetPosition.ToFloat() - mPreviousMousePos->ToFloat());
                float const lastDirectionLength = lastDirection.length();
                if (lastDirectionLength >= 1.5f) // Some tolerance
                {
                    mCurrentLockedDirection = lastDirection.normalise(lastDirectionLength);
                }
            }
            else if (!inputState.IsShiftKeyDown)
            {
                mCurrentLockedDirection.reset();
            }

            // Remember the next previous mouse position
            mPreviousMousePos = targetPosition;
        }
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        InitializeDown(inputState);

        // Set cursor
        SetCurrentCursor(inputState);
    }

    void OnLeftMouseUp(InputState const & inputState) override
    {
        // Stop sound
        mSoundController.StopSawSound();

        // Set cursor
        SetCurrentCursor(inputState);
    }

    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void InitializeDown(InputState const & inputState)
    {
        // Initialize state
        mPreviousMousePos = inputState.MousePosition;
        mCurrentLockedDirection.reset();
        mIsFirstSegment = true;
        mIsUnderwater = mGameController.IsUnderwater(inputState.MousePosition);

        // Start sound
        mSoundController.PlaySawSound(mIsUnderwater);
    }

    DisplayLogicalCoordinates CalculateTargetPosition(InputState const & inputState) const
    {
        if (mCurrentLockedDirection.has_value())
        {
            assert(mPreviousMousePos.has_value());

            auto const sourcePosition = mPreviousMousePos->ToFloat();
            auto const targetPosition =
                sourcePosition
                + *mCurrentLockedDirection * (inputState.MousePosition.ToFloat() - sourcePosition).dot(*mCurrentLockedDirection);

            return DisplayLogicalCoordinates::FromFloatRound(targetPosition);
        }
        else
        {
            return inputState.MousePosition;
        }
    }

    void SetCurrentCursor(InputState const & inputState)
    {
        if (inputState.IsLeftMouseDown)
        {
            // Set current cursor to the current down cursor
            mToolCursorManager.SetToolCursor(mDownCursorCounter % 2 ? mDownCursorImage2 : mDownCursorImage1);
        }
        else
        {
            // Set current cursor to the up cursor
            mToolCursorManager.SetToolCursor(mUpCursorImage);
        }
    }

    // Our cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage1;
    wxImage const mDownCursorImage2;

    //
    // State
    //

    // The previous mouse position; when set, we have a segment and can saw
    std::optional<DisplayLogicalCoordinates> mPreviousMousePos;

    // The current locked direction, in case locking is activated
    std::optional<vec2f> mCurrentLockedDirection;

    // True when this is the first mouse move of a series
    bool mIsFirstSegment;

    // The current counter for the down cursors
    uint8_t mDownCursorCounter;

    // The current above/underwaterness of the tool
    bool mIsUnderwater;
};

class HeatBlasterTool final : public Tool
{
public:

    HeatBlasterTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        mCurrentAction = inputState.IsShiftKeyDown ? HeatBlasterActionType::Cool : HeatBlasterActionType::Heat;

        if (inputState.IsLeftMouseDown)
        {
            mIsEngaged = mGameController.ApplyHeatBlasterAt(inputState.MousePosition, mCurrentAction);
        }
        else
        {
            mIsEngaged = false;
        }

        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        // Stop sound
        mSoundController.StopHeatBlasterSound();
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        bool isEngaged;
        HeatBlasterActionType currentAction = inputState.IsShiftKeyDown ? HeatBlasterActionType::Cool : HeatBlasterActionType::Heat;

        if (inputState.IsLeftMouseDown)
        {
            isEngaged = mGameController.ApplyHeatBlasterAt(inputState.MousePosition, mCurrentAction);
        }
        else
        {
            isEngaged = false;
        }

        if (isEngaged)
        {
            if (!mIsEngaged
                || mCurrentAction != currentAction)
            {
                // State change
                mIsEngaged = true;
                mCurrentAction = currentAction;

                // Start sound
                mSoundController.PlayHeatBlasterSound(mCurrentAction);

                // Update cursor
                SetCurrentCursor();
            }
        }
        else
        {
            if (mIsEngaged)
            {
                // State change
                mIsEngaged = false;

                // Stop sound
                mSoundController.StopHeatBlasterSound();

                // Update cursor
                SetCurrentCursor();
            }
            else if (mCurrentAction != currentAction)
            {
                // State change
                mCurrentAction = currentAction;

                // Update cursor
                SetCurrentCursor();
            }
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        if (mIsEngaged)
        {
            mToolCursorManager.SetToolCursor(mCurrentAction == HeatBlasterActionType::Cool ? mCoolDownCursorImage : mHeatDownCursorImage);
        }
        else
        {
            mToolCursorManager.SetToolCursor(mCurrentAction == HeatBlasterActionType::Cool ? mCoolUpCursorImage : mHeatUpCursorImage);
        }
    }

    // Our state
    bool mIsEngaged;
    HeatBlasterActionType mCurrentAction;

    // The cursors
    wxImage const mHeatUpCursorImage;
    wxImage const mCoolUpCursorImage;
    wxImage const mHeatDownCursorImage;
    wxImage const mCoolDownCursorImage;
};

class FireExtinguisherTool final : public Tool
{
public:

    FireExtinguisherTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        if (inputState.IsLeftMouseDown)
        {
            mIsEngaged = mGameController.ExtinguishFireAt(inputState.MousePosition);
        }
        else
        {
            mIsEngaged = false;
        }

        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        // Stop sound
        mSoundController.StopFireExtinguisherSound();
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        bool isEngaged;

        if (inputState.IsLeftMouseDown)
        {
            isEngaged = mGameController.ExtinguishFireAt(inputState.MousePosition);
        }
        else
        {
            isEngaged = false;
        }

        if (isEngaged)
        {
            if (!mIsEngaged)
            {
                // State change
                mIsEngaged = true;

                // Start sound
                mSoundController.PlayFireExtinguisherSound();

                // Update cursor
                SetCurrentCursor();
            }
        }
        else
        {
            if (mIsEngaged)
            {
                // State change
                mIsEngaged = false;

                // Stop sound
                mSoundController.StopFireExtinguisherSound();

                // Update cursor
                SetCurrentCursor();
            }
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        if (mIsEngaged)
        {
            mToolCursorManager.SetToolCursor(mDownCursorImage);
        }
        else
        {
            mToolCursorManager.SetToolCursor(mUpCursorImage);
        }
    }

    // Our state
    bool mIsEngaged;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};

class GrabTool final : public ContinuousTool
{
public:

    GrabTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        ContinuousTool::Initialize(inputState);

        if (inputState.IsLeftMouseDown)
        {
            // Start sound
            mSoundController.PlayDrawSound(mGameController.IsUnderwater(inputState.MousePosition));
        }

        SetBasisCursor(inputState);
    }

    void Deinitialize() override
    {
        // Stop sound
        mSoundController.StopDrawSound();
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        ContinuousTool::OnLeftMouseDown(inputState);

        // Start sound
        mSoundController.PlayDrawSound(mGameController.IsUnderwater(inputState.MousePosition));

        // Change cursor
        SetBasisCursor(inputState);
    }

    void OnLeftMouseUp(InputState const & inputState) override
    {
        // Stop sound
        mSoundController.StopDrawSound();

        // Change cursor
        SetBasisCursor(inputState);
    }

    void OnShiftKeyDown(InputState const & inputState) override
    {
        SetBasisCursor(inputState);
    }

    void OnShiftKeyUp(InputState const & inputState) override
    {
        SetBasisCursor(inputState);
    }

protected:

    void ApplyTool(
        std::chrono::microseconds const & cumulatedTime,
        InputState const & inputState) override;

private:

    void SetBasisCursor(InputState const & inputState)
    {
        if (inputState.IsLeftMouseDown)
        {
            if (inputState.IsShiftKeyDown)
            {
                // Down minus
                mToolCursorManager.SetToolCursor(mDownMinusCursorImage);
            }
            else
            {
                // Down plus
                mToolCursorManager.SetToolCursor(mDownPlusCursorImage);
            }
        }
        else
        {
            if (inputState.IsShiftKeyDown)
            {
                // Up minus
                mToolCursorManager.SetToolCursor(mUpMinusCursorImage);
            }
            else
            {
                // Up plus
                mToolCursorManager.SetToolCursor(mUpPlusCursorImage);
            }
        }
    }

    wxImage const mUpPlusCursorImage;
    wxImage const mUpMinusCursorImage;
    wxImage const mDownPlusCursorImage;
    wxImage const mDownMinusCursorImage;
};

class SwirlTool final : public ContinuousTool
{
public:

    SwirlTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        ContinuousTool::Initialize(inputState);

        if (inputState.IsLeftMouseDown)
        {
            // Start sound
            mSoundController.PlaySwirlSound(mGameController.IsUnderwater(inputState.MousePosition));
        }

        SetBasisCursor(inputState);
    }

    void Deinitialize() override
    {
        // Stop sound
        mSoundController.StopSwirlSound();
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        ContinuousTool::OnLeftMouseDown(inputState);

        // Start sound
        mSoundController.PlaySwirlSound(mGameController.IsUnderwater(inputState.MousePosition));

        // Change cursor
        SetBasisCursor(inputState);
    }

    void OnLeftMouseUp(InputState const & inputState) override
    {
        // Stop sound
        mSoundController.StopSwirlSound();

        // Change cursor
        SetBasisCursor(inputState);
    }

    void OnShiftKeyDown(InputState const & inputState) override
    {
        SetBasisCursor(inputState);
    }

    void OnShiftKeyUp(InputState const & inputState) override
    {
        SetBasisCursor(inputState);
    }

protected:

    void ApplyTool(
        std::chrono::microseconds const & cumulatedTime,
        InputState const & inputState) override;

private:

    void SetBasisCursor(InputState const & inputState)
    {
        if (inputState.IsLeftMouseDown)
        {
            if (inputState.IsShiftKeyDown)
            {
                // Down minus
                mToolCursorManager.SetToolCursor(mDownMinusCursorImage);
            }
            else
            {
                // Down plus
                mToolCursorManager.SetToolCursor(mDownPlusCursorImage);
            }
        }
        else
        {
            if (inputState.IsShiftKeyDown)
            {
                // Up minus
                mToolCursorManager.SetToolCursor(mUpMinusCursorImage);
            }
            else
            {
                // Up plus
                mToolCursorManager.SetToolCursor(mUpPlusCursorImage);
            }
        }
    }

    wxImage const mUpPlusCursorImage;
    wxImage const mUpMinusCursorImage;
    wxImage const mDownPlusCursorImage;
    wxImage const mDownMinusCursorImage;
};

class PinTool final : public OneShotTool
{
public:

    PinTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset cursor
        mToolCursorManager.SetToolCursor(mCursorImage);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        if (!inputState.IsShiftKeyDown)
        {
            // Toggle pin
            mGameController.TogglePinAt(inputState.MousePosition);
        }
        else
        {
            // Remove all pins
            mGameController.RemoveAllPins();
        }
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    wxImage const mCursorImage;
};

class InjectPressureTool final : public Tool
{
public:

    InjectPressureTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        if (mCurrentAction.has_value())
        {
            StopSounds(*mCurrentAction);
            mCurrentAction.reset();
        }
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        // Calculate new state
        std::optional<ActionType> newAction;
        if (inputState.IsLeftMouseDown)
        {
            auto const locus = mGameController.InjectPressureAt(
                inputState.MousePosition,
                inputState.IsShiftKeyDown ? -1.0f : 1.0f);

            if (locus.has_value())
            {
                if ((*locus & ToolApplicationLocus::Ship) == ToolApplicationLocus::Ship)
                    newAction = ActionType::Pressure;
                else if ((*locus & ToolApplicationLocus::World) == ToolApplicationLocus::World)
                    newAction = ActionType::AirBubble;
            }
        }

        // Transition state
        bool doUpdateCursor = false;
        if (newAction.has_value())
        {
            if (!mCurrentAction.has_value())
            {
                // Start sounds
                mSoundController.PlayPressureInjectionSound();
                if (*newAction == ActionType::AirBubble)
                {
                    mSoundController.PlayAirBubblesSound();
                }

                doUpdateCursor = true;
            }
            else if (*newAction != *mCurrentAction)
            {
                if (*mCurrentAction == ActionType::AirBubble)
                {
                    mSoundController.StopAirBubblesSound();
                }
                else
                {
                    assert(*newAction == ActionType::AirBubble);
                    mSoundController.PlayAirBubblesSound();
                }
            }
        }
        else
        {
            if (mCurrentAction.has_value())
            {
                // Stop sounds
                StopSounds(*mCurrentAction);

                doUpdateCursor = true;
            }
        }

        mCurrentAction = newAction;

        if (doUpdateCursor)
        {
            // Update cursor
            SetCurrentCursor();
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        mToolCursorManager.SetToolCursor(mCurrentAction.has_value() ? mDownCursorImage : mUpCursorImage);
    }

    enum class ActionType
    {
        Pressure,
        AirBubble
    };

    void StopSounds(ActionType action)
    {
        mSoundController.StopPressureInjectionSound();

        if (action == ActionType::AirBubble)
        {
            mSoundController.StopAirBubblesSound();
        }
    }

    // Our state
    std::optional<ActionType> mCurrentAction;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};

class FloodHoseTool final : public Tool
{
public:

    FloodHoseTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Update cursor
        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        // Stop sound
        mSoundController.StopFloodHoseSound();
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        bool isEngaged;
        if (inputState.IsLeftMouseDown)
        {
            isEngaged = mGameController.FloodAt(
                inputState.MousePosition,
                inputState.IsShiftKeyDown ? -1.0f : 1.0f);
        }
        else
        {
            isEngaged = false;
        }

        if (isEngaged)
        {
            if (!mIsEngaged)
            {
                // State change
                mIsEngaged = true;

                // Start sound
                mSoundController.PlayFloodHoseSound();
            }
            else
            {
                // Update down cursor
                ++mDownCursorCounter;
            }

            // Update cursor
            SetCurrentCursor();
        }
        else
        {
            if (mIsEngaged)
            {
                // State change
                mIsEngaged = false;

                // Stop sound
                mSoundController.StopFloodHoseSound();

                // Update cursor
                SetCurrentCursor();
            }
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        mToolCursorManager.SetToolCursor(
            mIsEngaged
            ? ((mDownCursorCounter % 2) ? mDownCursorImage2 : mDownCursorImage1)
            : mUpCursorImage);
    }

    // Our state
    bool mIsEngaged;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage1;
    wxImage const mDownCursorImage2;

    // The current counter for the down cursors
    uint8_t mDownCursorCounter;
};

class AntiMatterBombTool final : public OneShotTool
{
public:

    AntiMatterBombTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset cursor
        mToolCursorManager.SetToolCursor(mCursorImage);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Toggle bomb
        mGameController.ToggleAntiMatterBombAt(inputState.MousePosition);
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    wxImage const mCursorImage;
};

class ImpactBombTool final : public OneShotTool
{
public:

    ImpactBombTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset cursor
        mToolCursorManager.SetToolCursor(mCursorImage);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Toggle bomb
        mGameController.ToggleImpactBombAt(inputState.MousePosition);
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    wxImage const mCursorImage;
};

class RCBombTool final : public OneShotTool
{
public:

    RCBombTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset cursor
        mToolCursorManager.SetToolCursor(mCursorImage);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Toggle bomb
        mGameController.ToggleRCBombAt(inputState.MousePosition);
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    wxImage const mCursorImage;
};

class TimerBombTool final : public OneShotTool
{
public:

    TimerBombTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset cursor
        mToolCursorManager.SetToolCursor(mCursorImage);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Toggle bomb
        mGameController.ToggleTimerBombAt(inputState.MousePosition);
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    wxImage const mCursorImage;
};

class WaveMakerTool final : public OneShotTool
{
public:

    WaveMakerTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override 
    {
        mIsEngaged = false;
        SetCurrentCursor(inputState);
    }

    void Deinitialize() override
    {
        if (mIsEngaged)
        {
            mSoundController.StopWaveMakerSound();
        }
    }

    void UpdateSimulation(
        InputState const & inputState, 
        float /*currentSimulationTime*/) override
    {
        int constexpr PointerRadius = 0; // On PC we do not simulate a pointer size

        if (mIsEngaged)
        {
            if (inputState.IsLeftMouseDown)
            {
                mGameController.AdjustOceanSurfaceTo(inputState.MousePosition, PointerRadius);
            }
            else
            {
                // State change: stop

                mSoundController.StopWaveMakerSound();
                SetCurrentCursor(inputState);

                mIsEngaged = false;
            }
        }
        else
        {
            if (inputState.IsLeftMouseDown)
            {
                // State change: start
                mGameController.AdjustOceanSurfaceTo(inputState.MousePosition, PointerRadius);

                mSoundController.PlayWaveMakerSound();
                SetCurrentCursor(inputState);

                mIsEngaged = true;
            }
        }
    }

    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor(InputState const & inputState)
    {
        if (inputState.IsLeftMouseDown)
        {
            mToolCursorManager.SetToolCursor(mDownCursorImage);
        }
        else
        {
            mToolCursorManager.SetToolCursor(mUpCursorImage);
        }
    }

    // State
    bool mIsEngaged;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};

class TerrainAdjustTool final : public Tool
{
public:

    TerrainAdjustTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        mCurrentCursor = nullptr;

        if (inputState.IsLeftMouseDown)
        {
            mCurrentTrajectoryPreviousWorldPosition = mGameController.ScreenToWorld(inputState.MousePosition);
            SetCurrentCursor(&mDownCursorImage);
        }
        else
        {
            mCurrentTrajectoryPreviousWorldPosition.reset();
            SetCurrentCursor(&mUpCursorImage);
        }
    }

    void Deinitialize() override
    {
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        if (inputState.IsLeftMouseDown)
        {
            vec2f const mouseWorldPosition = mGameController.ScreenToWorld(inputState.MousePosition);

            if (!mCurrentTrajectoryPreviousWorldPosition)
            {
                mCurrentTrajectoryPreviousWorldPosition = mouseWorldPosition;
            }

            vec2f const targetPosition = inputState.IsShiftKeyDown
                ? vec2f(mouseWorldPosition.x, mCurrentTrajectoryPreviousWorldPosition->y)
                : mouseWorldPosition;

            auto const isAdjusted = mGameController.AdjustOceanFloorTo(
                *mCurrentTrajectoryPreviousWorldPosition,
                targetPosition);

            if (isAdjusted.has_value())
            {
                // Adjusted, eventually idempotent
                if (*isAdjusted)
                {
                    mSoundController.PlayTerrainAdjustSound();
                }

                SetCurrentCursor(&mDownCursorImage);
            }
            else
            {
                // No adjustment
                mSoundController.PlayErrorSound();
                SetCurrentCursor(&mErrorCursorImage);
            }

            // Remember this coordinate as the starting point of the
            // next stride
            mCurrentTrajectoryPreviousWorldPosition = targetPosition;
        }
        else
        {
            mCurrentTrajectoryPreviousWorldPosition.reset();
            SetCurrentCursor(&mUpCursorImage);
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor(wxImage const * cursor)
    {
        if (cursor != mCurrentCursor)
        {
            mToolCursorManager.SetToolCursor(*cursor);
            mCurrentCursor = cursor;
        }
    }

    // Our state
    std::optional<vec2f> mCurrentTrajectoryPreviousWorldPosition; // When set, indicates it's engaged

    // The cursors
    wxImage const * mCurrentCursor; // To track cursor changes
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
    wxImage const mErrorCursorImage;
};

class ScrubTool final : public Tool
{
public:

    ScrubTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        if (inputState.IsLeftMouseDown)
        {
            // Initialize state
            mPreviousMousePos = inputState.MousePosition;
        }
        else
        {
            // Reset state
            mPreviousMousePos = std::nullopt;
        }

        // Reset strike detection
        mPreviousStrikeVector.reset();
        mPreviousSoundTimestamp = std::chrono::steady_clock::time_point::min();

        // Set cursor
        SetCurrentCursor(inputState);
    }

    void Deinitialize() override {}

    void UpdateSimulation(InputState const & /*inputState*/, float /*currentSimulationTime*/) override {}

    void OnMouseMove(InputState const & inputState) override
    {
        if (inputState.IsLeftMouseDown)
        {
            if (!!mPreviousMousePos)
            {
                // Do a strike
                bool hasStriked;
                if (inputState.IsShiftKeyDown)
                {
                    hasStriked = mGameController.RotThrough(
                        *mPreviousMousePos,
                        inputState.MousePosition);
                }
                else
                {
                    hasStriked = mGameController.ScrubThrough(
                        *mPreviousMousePos,
                        inputState.MousePosition);
                }

                // See if we should emit a sound
                if (hasStriked)
                {
                    vec2f const newStrikeVector = (inputState.MousePosition - *mPreviousMousePos).ToFloat();
                    if (newStrikeVector.length() > 1.0f)
                    {
                        auto const now = std::chrono::steady_clock::now();

                        if (!mPreviousStrikeVector
                            || std::abs(mPreviousStrikeVector->angleCw(newStrikeVector)) > Pi<float> / 2.0f    // Direction change
                            || (now - mPreviousSoundTimestamp) > std::chrono::milliseconds(250))
                        {
                            // Play sound
                            if (inputState.IsShiftKeyDown)
                                mSoundController.PlayRotSound();
                            else
                                mSoundController.PlayScrubSound();

                            mPreviousStrikeVector = newStrikeVector;
                            mPreviousSoundTimestamp = now;
                        }
                    }
                }
            }

            // Remember the next previous mouse position
            mPreviousMousePos = inputState.MousePosition;
        }
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Initialize state
        mPreviousMousePos = inputState.MousePosition;
        mPreviousStrikeVector.reset();
        mPreviousSoundTimestamp = std::chrono::steady_clock::time_point::min();

        // Set cursor
        SetCurrentCursor(inputState);
    }

    void OnLeftMouseUp(InputState const & inputState) override
    {
        // Reset state
        mPreviousMousePos = std::nullopt;

        // Set cursor
        SetCurrentCursor(inputState);
    }

    void OnShiftKeyDown(InputState const & inputState) override
    {
        // Set cursor
        SetCurrentCursor(inputState);
    }

    void OnShiftKeyUp(InputState const & inputState) override
    {
        // Set cursor
        SetCurrentCursor(inputState);
    }

private:

    void SetCurrentCursor(InputState const & inputState)
    {
        if (inputState.IsLeftMouseDown)
        {
            // Set current cursor to the down cursor
            if (inputState.IsShiftKeyDown)
            {
                mToolCursorManager.SetToolCursor(mRotDownCursorImage);
            }
            else
            {
                mToolCursorManager.SetToolCursor(mScrubDownCursorImage);
            }
        }
        else
        {
            // Set current cursor to the up cursor
            if (inputState.IsShiftKeyDown)
            {
                mToolCursorManager.SetToolCursor(mRotUpCursorImage);
            }
            else
            {
                mToolCursorManager.SetToolCursor(mScrubUpCursorImage);
            }
        }
    }

    // Our cursors
    wxImage const mScrubUpCursorImage;
    wxImage const mScrubDownCursorImage;
    wxImage const mRotUpCursorImage;
    wxImage const mRotDownCursorImage;

    //
    // State
    //

    // The previous mouse position; when set, we have a segment and can strike
    std::optional<DisplayLogicalCoordinates> mPreviousMousePos;

    // The previous strike vector, which we want to remember in order to
    // detect directions changes for the scrubbing/rotting sound
    std::optional<vec2f> mPreviousStrikeVector;

    // The time at which we have last played a sound
    std::chrono::steady_clock::time_point mPreviousSoundTimestamp;
};

class RepairStructureTool final : public Tool
{
public:

    RepairStructureTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset state
        mEngagementStartTimestamp.reset();

        // Update cursor
        UpdateCurrentCursor();
    }

    void Deinitialize() override
    {
        // Stop sound, just in case
        mSoundController.StopRepairStructureSound();
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        bool isEngaged;
        if (inputState.IsLeftMouseDown)
        {
            isEngaged = true;
        }
        else
        {
            isEngaged = false;
        }

        if (isEngaged)
        {
            if (!mEngagementStartTimestamp)
            {
                // State change
                ++mCurrentStepId; // Skip one step so that this looks like a new session
                mEngagementStartTimestamp = std::chrono::steady_clock::now();

                // Start sound
                mSoundController.PlayRepairStructureSound();
            }

            // Increment step
            ++mCurrentStepId;

            // Repair
            mGameController.RepairAt(
                inputState.MousePosition,
                1.0f,
                mCurrentStepId);

            // Update cursor
            UpdateCurrentCursor();
        }
        else
        {
            if (!!mEngagementStartTimestamp)
            {
                // State change
                mEngagementStartTimestamp.reset();

                // Stop sound
                mSoundController.StopRepairStructureSound();

                // Update cursor
                UpdateCurrentCursor();
            }
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void UpdateCurrentCursor()
    {
        wxImage cursorImage;

        if (!!mEngagementStartTimestamp)
        {
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *mEngagementStartTimestamp);

            // Synchronize with sound
            auto cursorPhase = (totalElapsed.count() % 1000);
            if (cursorPhase < 87)
                cursorImage = mDownCursorImages[0]; // |
            else if (cursorPhase < 175)
                cursorImage = mDownCursorImages[1]; //
            else if (cursorPhase < 237)
                cursorImage = mDownCursorImages[2]; // /* \ */
            else if (cursorPhase < 300)
                cursorImage = mDownCursorImages[3]; //
            else if (cursorPhase < 500)
                cursorImage = mDownCursorImages[4]; // _
            else if (cursorPhase < 526)
                cursorImage = mDownCursorImages[3]; //
            else if (cursorPhase < 553)
                cursorImage = mDownCursorImages[2]; // /* \ */
            else if (cursorPhase < 580)
                cursorImage = mDownCursorImages[1]; //
            else
                cursorImage = mDownCursorImages[0]; // |
        }
        else
        {
            cursorImage = mUpCursorImage;
        }

        mToolCursorManager.SetToolCursor(cursorImage);
    }

private:

    // When set, we are engaged
    std::optional<std::chrono::steady_clock::time_point> mEngagementStartTimestamp;

    // The current repair step id
    SequenceNumber mCurrentStepId;

    // Our cursors
    wxImage const mUpCursorImage;
    std::array<wxImage, 5> const mDownCursorImages;
};

class ThanosSnapTool final : public OneShotTool
{
public:

    ThanosSnapTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        // Reset cursor
        SetCurrentCursor(inputState);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Do snap
        mGameController.ApplyThanosSnapAt(inputState.MousePosition);
        mSoundController.PlayThanosSnapSound();

        // Update cursor
        SetCurrentCursor(inputState);
    }

    void OnLeftMouseUp(InputState const & inputState) override
    {
        // Update cursor
        SetCurrentCursor(inputState);
    }

private:

    void SetCurrentCursor(InputState const & inputState)
    {
        mToolCursorManager.SetToolCursor(inputState.IsLeftMouseDown ? mDownCursorImage : mUpCursorImage);
    }

    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};

class ScareFishTool final : public Tool
{
public:

    ScareFishTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        mIsEngaged = inputState.IsLeftMouseDown;
        mCurrentAction = inputState.IsShiftKeyDown ? ActionType::Attract : ActionType::Scare;

        // Update cursor
        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        if (mIsEngaged)
        {
            // Stop sound
            StopSound(mCurrentAction);

            mIsEngaged = false;
        }
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        // Calculate new state and eventually apply action
        bool newIsEngaged = inputState.IsLeftMouseDown;
        ActionType const newAction = inputState.IsShiftKeyDown ? ActionType::Attract : ActionType::Scare;
        if (newIsEngaged)
        {
            switch (newAction)
            {
                case ActionType::Attract:
                {
                    mGameController.AttractFish(
                        inputState.MousePosition,
                        50.0f, // Radius
                        std::chrono::milliseconds(0)); // Delay

                    break;
                }

                case ActionType::Scare:
                {
                    mGameController.ScareFish(
                        inputState.MousePosition,
                        40.0f, // Radius
                        std::chrono::milliseconds(0)); // Delay

                    break;
                }
            }
        }

        // Transition state
        bool doUpdateCursor = false;
        if (newIsEngaged)
        {
            if (!mIsEngaged)
            {
                // Start sound
                StartSound(newAction);

                doUpdateCursor = true;
            }
            else
            {
                if (newAction != mCurrentAction)
                {
                    // Change sound
                    StopSound(mCurrentAction);
                    StartSound(newAction);
                }

                // Update down cursor
                ++mDownCursorCounter;

                doUpdateCursor = true;
            }
        }
        else
        {
            if (mIsEngaged)
            {
                // Stop sound
                StopSound(mCurrentAction);

                doUpdateCursor = true;
            }
            else
            {
                if (newAction != mCurrentAction)
                {
                    doUpdateCursor = true;
                }
            }
        }

        mIsEngaged = newIsEngaged;
        mCurrentAction = newAction;

        if (doUpdateCursor)
        {
            // Update cursor
            SetCurrentCursor();
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    enum class ActionType
    {
        Scare,
        Attract
    };

    void SetCurrentCursor()
    {
        switch (mCurrentAction)
        {
            case ActionType::Attract:
            {
                mToolCursorManager.SetToolCursor(
                    mIsEngaged
                    ? ((mDownCursorCounter % 2) ? mAttractDownCursorImage2 : mAttractDownCursorImage1)
                    : mAttractUpCursorImage);

                break;
            }

            case ActionType::Scare:
            {
                mToolCursorManager.SetToolCursor(
                    mIsEngaged
                    ? ((mDownCursorCounter % 2) ? mScareDownCursorImage2 : mScareDownCursorImage1)
                    : mScareUpCursorImage);

                break;
            }
        }
    }

    void StartSound(ActionType action)
    {
        switch (action)
        {
            case ActionType::Attract:
            {
                mSoundController.PlayFishFoodSound();
                break;
            }

            case ActionType::Scare:
            {
                mSoundController.PlayFishScareSound();
                break;
            }
        }
    }

    void StopSound(ActionType action)
    {
        switch (action)
        {
            case ActionType::Attract:
            {
                mSoundController.StopFishFoodSound();
                break;
            }

            case ActionType::Scare:
            {
                mSoundController.StopFishScareSound();
                break;
            }
        }
    }

    // Our state
    bool mIsEngaged;
    ActionType mCurrentAction;

    // The cursors
    wxImage const mScareUpCursorImage;
    wxImage const mScareDownCursorImage1;
    wxImage const mScareDownCursorImage2;
    wxImage const mAttractUpCursorImage;
    wxImage const mAttractDownCursorImage1;
    wxImage const mAttractDownCursorImage2;

    // The current counter for the down cursors
    uint8_t mDownCursorCounter;
};

class PhysicsProbeTool final : public OneShotTool
{
public:

    PhysicsProbeTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset cursor
        mToolCursorManager.SetToolCursor(mCursorImage);
    }

    void OnLeftMouseDown(InputState const & inputState) override
    {
        // Toggle probe
        mGameController.TogglePhysicsProbeAt(inputState.MousePosition);
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

private:

    wxImage const mCursorImage;
};

class BlastTool final : public Tool
{
public:

    BlastTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & inputState) override
    {
        mEngagementData.reset();

        SetCurrentCursor(inputState.IsShiftKeyDown);
    }

    void Deinitialize() override {}

    void UpdateSimulation(InputState const & inputState, float currentSimulationTime) override
    {
        if (inputState.IsLeftMouseDown
            && !mEngagementData.has_value())
        {
            //
            // Start
            //

            bool const isBoosted = inputState.IsShiftKeyDown;

            mEngagementData.emplace(
                currentSimulationTime,
                isBoosted,
                GameRandomEngine::GetInstance().GenerateNormalizedUniformReal());

            if (isBoosted)
                mSoundController.PlayBlastToolFastSound();
            else
                mSoundController.PlayBlastToolSlow1Sound();

            SetCurrentCursor(isBoosted);
        }

        if (mEngagementData.has_value())
        {
            //
            // Progress state machine
            //

            float const elapsed = currentSimulationTime - mEngagementData->CurrentStateStartSimulationTime;
            assert(elapsed >= 0.0f);

            switch (mEngagementData->CurrentState)
            {
                case EngagementData::StateType::NormalPhase1:
                {
                    float constexpr BlastDuration1 = 0.5f;

                    float const progress = std::min(elapsed / BlastDuration1, 1.0f);

                    mGameController.ApplyBlastAt(
                        inputState.MousePosition,
                        progress,
                        1.0f,
                        progress,
                        mEngagementData->PersonalitySeed);

                    if (progress == 1.0f)
                    {
                        // Transition
                        mEngagementData->CurrentState = EngagementData::StateType::NormalPhase2;
                        mEngagementData->CurrentStateStartSimulationTime = currentSimulationTime;
                    }

                    break;
                }

                case EngagementData::StateType::NormalPhase2:
                {
                    float constexpr BlastDurationPause = 1.5f;

                    float const progress = std::min(elapsed / BlastDurationPause, 1.0f);

                    if (progress == 1.0f)
                    {
                        // Transition
                        mEngagementData->CurrentState = EngagementData::StateType::NormalPhase3;
                        mEngagementData->CurrentStateStartSimulationTime = currentSimulationTime;

                        // Emit sound
                        mSoundController.PlayBlastToolSlow2Sound();
                    }

                    break;
                }

                case EngagementData::StateType::NormalPhase3:
                {
                    float constexpr BlastDuration2 = 0.25f;

                    float const progress = std::min(elapsed / BlastDuration2, 1.0f);

                    mGameController.ApplyBlastAt(
                        inputState.MousePosition,
                        1.0f + progress * 2.5f,
                        2.0f + 0.5f,
                        progress,
                        mEngagementData->PersonalitySeed);

                    if (progress == 1.0f)
                    {
                        // Transition
                        mEngagementData->CurrentState = EngagementData::StateType::NormalCompleted;
                    }

                    break;
                }

                case EngagementData::StateType::NormalCompleted:
                {
                    // Nop
                    break;
                }

                case EngagementData::StateType::BoostedPhase1:
                {
                    float constexpr BoostedBlastDuration = 0.25f;

                    float const progress = std::min(elapsed / BoostedBlastDuration, 1.0f);

                    mGameController.ApplyBlastAt(
                        inputState.MousePosition,
                        progress * 2.0f,
                        2.0f,
                        progress,
                        mEngagementData->PersonalitySeed);

                    if (progress == 1.0f)
                    {
                        // Transition
                        mEngagementData->CurrentState = EngagementData::StateType::BoostedCompleted;
                        mEngagementData->CurrentStateStartSimulationTime = currentSimulationTime;
                    }

                    break;
                }

                case EngagementData::StateType::BoostedCompleted:
                {
                    // Nop
                    break;
                }
            }
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}

    void OnLeftMouseUp(InputState const & inputState) override
    {
        //
        // Restore to normal
        //

        mEngagementData.reset();

        SetCurrentCursor(inputState.IsShiftKeyDown);
    }

    void OnShiftKeyDown(InputState const & inputState) override
    {
        SetCurrentCursor(inputState.IsShiftKeyDown);
    }

    void OnShiftKeyUp(InputState const & inputState) override
    {
        SetCurrentCursor(inputState.IsShiftKeyDown);
    }

private:

    void SetCurrentCursor(bool isShiftKey)
    {
        if (mEngagementData.has_value())
        {
            mToolCursorManager.SetToolCursor(mDownCursorImage);
        }
        else
        {
            if (!isShiftKey)
                mToolCursorManager.SetToolCursor(mUpCursorImage1);
            else
                mToolCursorManager.SetToolCursor(mUpCursorImage2);
        }
    }

    struct EngagementData
    {
        enum class StateType
        {
            NormalPhase1,
            NormalPhase2,
            NormalPhase3,
            NormalCompleted,
            BoostedPhase1,
            BoostedCompleted
        };

        float CurrentStateStartSimulationTime;
        StateType CurrentState;

        float const PersonalitySeed;

        EngagementData(
            float startSimulationTime,
            bool isBoosted,
            float personalitySeed)
            : CurrentStateStartSimulationTime(startSimulationTime)
            , CurrentState(isBoosted ? StateType::BoostedPhase1 : StateType::NormalPhase1)
            , PersonalitySeed(personalitySeed)
        { }
    };

    // Our state
    std::optional<EngagementData> mEngagementData;

    // The cursors
    wxImage const mUpCursorImage1;
    wxImage const mUpCursorImage2;
    wxImage const mDownCursorImage;
};

class ElectricSparkTool final : public Tool
{
public:

    ElectricSparkTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Update cursor
        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        // Stop sound
        mSoundController.StopElectricSparkSound();
    }

    void UpdateSimulation(InputState const & inputState, float currentSimulationTime) override
    {
        if (inputState.IsLeftMouseDown)
        {
            float const interactionLengthMultiplier = (inputState.IsShiftKeyDown) ? 3.2f : 1.0f;

            if (mEngagementData.has_value())
            {
                // We are currently engaged

                // Send interaction
                bool hasBeenApplied = mGameController.ApplyElectricSparkAt(
                    inputState.MousePosition,
                    ++(mEngagementData->CurrentCounter),
                    mEngagementData->CurrentLengthMultiplier,
                    currentSimulationTime);

                if (!hasBeenApplied)
                {
                    //
                    // State change: engaged -> not engaged
                    //

                    Disengage();
                }
                else
                {
                    // Check if we need to change the underwater-ness of the sound
                    bool isUnderwater = mGameController.IsUnderwater(inputState.MousePosition);
                    if (isUnderwater != mEngagementData->IsUnderwater)
                    {
                        mSoundController.PlayElectricSparkSound(isUnderwater);
                        mEngagementData->IsUnderwater = isUnderwater;
                    }

                    // Update towards target
                    mEngagementData->CurrentLengthMultiplier +=
                        0.18f
                        * (interactionLengthMultiplier - mEngagementData->CurrentLengthMultiplier);
                }
            }
            else
            {
                // We are currently not engaged

                // Send first interaction, together with probe
                bool hasBeenApplied = mGameController.ApplyElectricSparkAt(
                    inputState.MousePosition,
                    0,
                    interactionLengthMultiplier,
                    currentSimulationTime);

                if (hasBeenApplied)
                {
                    //
                    // State change: not engaged -> engaged
                    //

                    bool const isUnderwater = mGameController.IsUnderwater(inputState.MousePosition);

                    mEngagementData.emplace(
                        0,  // counter
                        interactionLengthMultiplier,
                        isUnderwater);

                    mSoundController.PlayElectricSparkSound(isUnderwater);

                    SetCurrentCursor();
                }
            }
        }
        else
        {
            if (mEngagementData.has_value())
            {
                //
                // State change: engaged -> not engaged
                //

                Disengage();
            }
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}
    void OnLeftMouseDown(InputState const & /*inputState*/) override {}
    void OnLeftMouseUp(InputState const & /*inputState*/) override {}
    void OnShiftKeyDown(InputState const & /*inputState*/) override {}
    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void Disengage()
    {
        mEngagementData.reset();

        mSoundController.StopElectricSparkSound();

        SetCurrentCursor();
    }

    void SetCurrentCursor()
    {
        mToolCursorManager.SetToolCursor(
            mEngagementData.has_value()
            ? mDownCursorImage
            : mUpCursorImage);
    }

    struct EngagementData
    {
        std::uint64_t CurrentCounter;
        float CurrentLengthMultiplier;
        bool IsUnderwater;

        EngagementData(
            std::uint64_t initialCounter,
            float initialLengthMultiplier,
            bool isUnderwater)
            : CurrentCounter(initialCounter)
            , CurrentLengthMultiplier(initialLengthMultiplier)
            , IsUnderwater(isUnderwater)
        {}
    };

    // Our state
    std::optional<EngagementData> mEngagementData;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};

class WindMakerTool final : public Tool
{
public:

    WindMakerTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        // Reset state
        mEngagementData.reset();

        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        if (mEngagementData.has_value())
        {
            mSoundController.StopWindMakerWindSound();
        }
    }

    void UpdateSimulation(InputState const & inputState, float currentSimulationTime) override
    {
        bool doUpdateCursor = false;

        if (!mEngagementData.has_value())
        {
            if (inputState.IsLeftMouseDown)
            {
                //
                // Start engagement
                //

                mEngagementData.emplace(currentSimulationTime);

                if (inputState.IsShiftKeyDown)
                {
                    mSoundController.PlayWindGustShortSound();
                }

                doUpdateCursor = true;
            }
        }
        else
        {
            //
            // Update cursor
            //

            float const totalElapsed = mEngagementData->GetElapsedPreFrontSimulationTime(currentSimulationTime);
            int cursorIndex = static_cast<int>(totalElapsed / (inputState.IsShiftKeyDown ? 0.025f : 0.05f)) % 3;
            if (cursorIndex != mEngagementData->CurrentCursorIndex)
            {
                mEngagementData->CurrentCursorIndex = cursorIndex;
                doUpdateCursor = true;
            }

            //
            // Update state machine
            //

            if (((mEngagementData->CurrentState == EngagementData::StateType::PreFront) || (mEngagementData->CurrentState == EngagementData::StateType::MainFront))
                && !inputState.IsLeftMouseDown)
            {
                // User left the mouse button...
                // ...transition to tear down
                mEngagementData->TransitionToTearDown(currentSimulationTime, inputState.MousePosition);
            }

            DisplayLogicalCoordinates centerPosition(0, 0);
            float intensity = 0.0f;

            switch (mEngagementData->CurrentState)
            {
                case EngagementData::StateType::PreFront:
                {
                    centerPosition = inputState.MousePosition;

                    float const stateDuration = inputState.IsShiftKeyDown
                        ? 0.050f // 50ms
                        : 1.5f; // 1.5s

                    auto const elapsed = mEngagementData->GetElapsedPreFrontSimulationTime(currentSimulationTime);

                    if (elapsed >= stateDuration)
                    {
                        intensity = 1.0f;

                        // Transition
                        mEngagementData->CurrentState = EngagementData::StateType::MainFront;
                        mEngagementData->MainFrontStartSimulationTime = currentSimulationTime;
                    }
                    else
                    {
                        intensity = elapsed / stateDuration;
                    }

                    break;
                }

                case EngagementData::StateType::MainFront:
                {
                    centerPosition = inputState.MousePosition;

                    intensity = 1.0f;

                    break;
                }

                case EngagementData::StateType::TearDown:
                {
                    assert(mEngagementData->TearDownCenterPosition.has_value());
                    centerPosition = *(mEngagementData->TearDownCenterPosition);

                    float constexpr TearDownDuration = 2.0f;

                    float const elapsed = mEngagementData->GetElapsedTearDownSimulationTime(currentSimulationTime);

                    // See if we're done
                    if (elapsed >= TearDownDuration)
                    {
                        intensity = 0.0f;

                        // Done!
                        mEngagementData.reset();
                        doUpdateCursor = true;
                    }
                    else
                    {
                        intensity = 1.0f - elapsed / TearDownDuration;
                    }

                    break;
                }
            }

            // Calculate intensity multipliers
            float const preFrontIntensityMultiplier = intensity * (inputState.IsShiftKeyDown ? 3.0f : 1.0f);
            float const mainFrontIntensityMultiplier = intensity * (inputState.IsShiftKeyDown ? 1.5f : 1.0f);

            // Invoke world
            mGameController.ApplyRadialWindFrom(
                centerPosition,
                mEngagementData->GetElapsedPreFrontSimulationTime(currentSimulationTime),
                preFrontIntensityMultiplier,
                mEngagementData->GetElapsedMainFrontSimulationTime(currentSimulationTime),
                mainFrontIntensityMultiplier);

            // Sound
            mSoundController.PlayOrUpdateWindMakerWindSound(mainFrontIntensityMultiplier * 50.0f);
        }

        if (doUpdateCursor)
        {
            SetCurrentCursor();
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}

    void OnLeftMouseDown(InputState const & /*inputState*/) override
    {
        // Whatever our state, let's restart from scratch
        mEngagementData.reset();
    }

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

    void OnShiftKeyDown(InputState const & /*inputState*/) override 
    {
        if (mEngagementData.has_value()
            && mEngagementData->CurrentState != EngagementData::StateType::TearDown)
        {
            mSoundController.PlayWindGustShortSound();
        }
    }

    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        if (!mEngagementData.has_value()
            || mEngagementData->CurrentState == EngagementData::StateType::TearDown)
        {
            mToolCursorManager.SetToolCursor(mUpCursorImage);
        }
        else
        {
            mToolCursorManager.SetToolCursor(mDownCursorImages[mEngagementData->CurrentCursorIndex]);
        }
    }

    struct EngagementData
    {
        enum class StateType
        {
            PreFront,
            MainFront,
            TearDown
        };

        StateType CurrentState;

        float PreFrontStartSimulationTime;
        std::optional<float> MainFrontStartSimulationTime;
        std::optional<float> TearDownStartSimulationTime;

        std::optional<DisplayLogicalCoordinates> TearDownCenterPosition;

        int CurrentCursorIndex;

        EngagementData(float startSimulationTime)
            : CurrentState(StateType::PreFront)
            , PreFrontStartSimulationTime(startSimulationTime)
            , MainFrontStartSimulationTime()
            , TearDownStartSimulationTime()
            , TearDownCenterPosition()
            , CurrentCursorIndex(0)
        { }

        void TransitionToTearDown(
            float startSimulationTime,
            DisplayLogicalCoordinates const & position)
        {
            CurrentState = StateType::TearDown;
            TearDownStartSimulationTime = startSimulationTime;
            TearDownCenterPosition = position;
        }

        float GetElapsedPreFrontSimulationTime(float currentSimulationTime) const
        {
            return std::max(currentSimulationTime - PreFrontStartSimulationTime, 0.0f);
        }

        float GetElapsedMainFrontSimulationTime(float currentSimulationTime) const
        {
            return MainFrontStartSimulationTime.has_value()
                ? currentSimulationTime - *MainFrontStartSimulationTime
                : 0.0f;
        }

        float GetElapsedTearDownSimulationTime(float currentSimulationTime) const
        {
            return TearDownStartSimulationTime.has_value()
                ? currentSimulationTime - *TearDownStartSimulationTime
                : 0.0f;
        }
    };

    // Our state
    std::optional<EngagementData> mEngagementData;

    // The cursors
    wxImage const mUpCursorImage;
    std::array<wxImage, 3> mDownCursorImages;
};

class LaserCannonTool final : public Tool
{
public:

    LaserCannonTool(
        IToolCursorManager & toolCursorManager,
        IGameController & gameController,
        SoundController & soundController,
        ResourceLocator const & resourceLocator);

public:

    void Initialize(InputState const & /*inputState*/) override
    {
        mEngagementData.reset();

        SetCurrentCursor();
    }

    void Deinitialize() override
    {
        if (mEngagementData.has_value())
        {
            // Stop sound
            mSoundController.StopLaserRaySound();
        }
    }

    void UpdateSimulation(InputState const & inputState, float /*currentSimulationTime*/) override
    {
        bool const isAmplified = inputState.IsShiftKeyDown;

        bool doUpdateCursor = false;

        if (inputState.IsLeftMouseDown)
        {
            // Update engagement state
            bool doUpdateSound = false;
            if (!mEngagementData.has_value())
            {
                // Engage
                mEngagementData.emplace(isAmplified, inputState.MousePosition);
                doUpdateCursor = true;

                // Update sound
                doUpdateSound = true;
            }
            else if (mEngagementData->IsAmplified != isAmplified)
            {
                // Update state
                mEngagementData->IsAmplified = isAmplified;

                // Update sound
                doUpdateSound = true;
            }

            // Apply interaction
            mGameController.ApplyLaserCannonThrough(
                mEngagementData->PreviousMousePosition,
                inputState.MousePosition,
                isAmplified ? 2.0f : 1.0f);

            // Update state
            mEngagementData->PreviousMousePosition = inputState.MousePosition;

            // Update sound
            if (doUpdateSound)
            {
                mSoundController.PlayLaserRaySound(isAmplified);
            }
        }
        else
        {
            // Update engagement state
            if (mEngagementData.has_value())
            {
                // Stop sound
                mSoundController.StopLaserRaySound();

                // Disengage
                mEngagementData.reset();
                doUpdateCursor = true;
            }

            // Apply interaction
            mGameController.ApplyLaserCannonThrough(
                inputState.MousePosition,
                inputState.MousePosition,
                std::nullopt);
        }

        // Update cursor
        if (doUpdateCursor)
        {
            SetCurrentCursor();
        }
    }

    void OnMouseMove(InputState const & /*inputState*/) override {}

    void OnLeftMouseDown(InputState const & /*inputState*/) override {}

    void OnLeftMouseUp(InputState const & /*inputState*/) override {}

    void OnShiftKeyDown(InputState const & /*inputState*/) override {}

    void OnShiftKeyUp(InputState const & /*inputState*/) override {}

private:

    void SetCurrentCursor()
    {
        if (!mEngagementData.has_value())
        {
            mToolCursorManager.SetToolCursor(mUpCursorImage);
        }
        else
        {
            mToolCursorManager.SetToolCursor(mDownCursorImage);
        }
    }

    struct EngagementData
    {
        bool IsAmplified;
        DisplayLogicalCoordinates PreviousMousePosition;

        EngagementData(
            bool isAmplified,
            DisplayLogicalCoordinates const & previousMousePosition)
            : IsAmplified(isAmplified)
            , PreviousMousePosition(previousMousePosition)
        {}
    };

    std::optional<EngagementData> mEngagementData;

    // The cursors
    wxImage const mUpCursorImage;
    wxImage const mDownCursorImage;
};
