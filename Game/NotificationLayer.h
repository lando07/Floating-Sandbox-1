/***************************************************************************************
* Original Author:      Gabriele Giuseppini
* Created:              2019-10-23
* Copyright:            Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "GameEventDispatcher.h"
#include "PerfStats.h"
#include "RenderContext.h"

#include <GameCore/GameTypes.h>

#include <array>
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <vector>

class NotificationLayer final : private IGenericGameEventHandler
{
public:

	NotificationLayer(
		bool isUltraViolentMode,
		bool isSoundMuted,
		bool isDayLightCycleOn,
		UnitsSystem displayUnitsSystem,
		std::shared_ptr<GameEventDispatcher> gameEventDispatcher);

	bool IsStatusTextEnabled() const { return mIsStatusTextEnabled; }
	void SetStatusTextEnabled(bool isEnabled);

	bool IsExtendedStatusTextEnabled() const { return mIsExtendedStatusTextEnabled; }
	void SetExtendedStatusTextEnabled(bool isEnabled);

    void SetStatusTexts(
        float immediateFps,
        float averageFps,
        PerfStats const & lastDeltaPerfStats,
        PerfStats const & totalPerfStats,
        std::chrono::duration<float> elapsedGameSeconds,
        bool isPaused,
        float zoom,
        vec2f const & camera,
        Render::RenderStatistics renderStats);

	void AddEphemeralTextLine(
		std::string const & text,
		std::chrono::duration<float> lifetime = std::chrono::duration<float>(1.0f));

	void SetUltraViolentModeIndicator(bool isUltraViolentMode);

	void SetSoundMuteIndicator(bool isSoundMuted);

	void SetDayLightCycleIndicator(bool isDayLightCycleOn);

	void SetPhysicsProbePanelState(float targetOpen);

	void SetDisplayUnitsSystem(UnitsSystem value);

	// One frame only; after RenderUpload() it's gone
	inline void SetHeatBlaster(
		vec2f const & worldCoordinates,
		float radius,
		HeatBlasterActionType action)
	{
		mHeatBlasterFlameToRender.emplace(
			worldCoordinates,
			radius,
			action);
	}

	// One frame only; after RenderUpload() it's gone
	inline void SetFireExtinguisherSpray(
		vec2f const & worldCoordinates,
		float radius)
	{
		mFireExtinguisherSprayToRender.emplace(
			worldCoordinates,
			radius);
	}

	// One frame only; after RenderUpload() it's gone
	// (special case as this is really UI)
	inline void SetBlastToolHalo(
		vec2f const & worldCoordinates,
		float radius,
		float renderProgress,
		float personalitySeed)
	{
		mBlastToolHaloToRender.emplace(
			worldCoordinates,
			radius,
			renderProgress,
			personalitySeed);
	}

	void Reset();

    void Update(float now);

	void RenderUpload(Render::RenderContext & renderContext);

private:

	//
	// IGenericGameEventHandler
	//

	void OnPhysicsProbeReading(
		vec2f const & velocity,
		float temperature,
		float depth) override;

private:

	void UploadStatusTextLine(
		std::string & line,
		bool isEnabled,
		int & effectiveOrdinal,
		Render::NotificationRenderContext & notificationRenderContext);

	void RegeneratePhysicsProbeReadingStrings();

private:

	std::shared_ptr<GameEventDispatcher> mGameEventDispatcher;

	//
	// Status text
	//

    bool mIsStatusTextEnabled;
    bool mIsExtendedStatusTextEnabled;
	std::array<std::string, 4> mStatusTextLines;
	bool mIsStatusTextDirty;

	//
	// Notification text
	//

	struct EphemeralTextLine
	{
		std::string Text;
		std::chrono::duration<float> Lifetime;

		enum class StateType
		{
			Initial,
			FadingIn,
			Displaying,
			FadingOut,
			Disappearing
		};

		StateType State;
		float CurrentStateStartTimestamp;
		float CurrentStateProgress;

		EphemeralTextLine(
			std::string const & text,
			std::chrono::duration<float> const lifetime)
			: Text(text)
			, Lifetime(lifetime)
			, State(StateType::Initial)
			, CurrentStateStartTimestamp(0.0f)
			, CurrentStateProgress(0.0f)
		{}

		EphemeralTextLine(EphemeralTextLine && other) = default;
		EphemeralTextLine & operator=(EphemeralTextLine && other) = default;
	};

	std::deque<EphemeralTextLine> mEphemeralTextLines; // Ordered from top to bottom
	bool mIsNotificationTextDirty;

	//
	// Texture notifications
	//

	bool mIsUltraViolentModeIndicatorOn;
	bool mIsSoundMuteIndicatorOn;
	bool mIsDayLightCycleOn;
	bool mAreTextureNotificationsDirty;

	//
	// Physics probe
	//

	struct PhysicsProbePanelState
	{
		static std::chrono::duration<float> constexpr OpenDelayDuration = std::chrono::duration<float>(0.5f);
		static std::chrono::duration<float> constexpr TransitionDuration = std::chrono::duration<float>(2.1f); // After open delay

		float CurrentOpen;
		float TargetOpen;
		float CurrentStateStartTime;

		PhysicsProbePanelState()
		{
			Reset();
		}

		void Reset()
		{
			CurrentOpen = 0.0f;
			TargetOpen = 0.0f;
			CurrentStateStartTime = 0.0f;
		}
	};

	PhysicsProbePanelState mPhysicsProbePanelState;
	bool mIsPhysicsProbePanelDirty;

	struct PhysicsProbeReading
	{
		float Speed;
		float Temperature;
		float Depth;
	};

	PhysicsProbeReading mPhysicsProbeReading; // Storage for raw reading values

	struct PhysicsProbeReadingStrings
	{
		std::string Speed;
		std::string Temperature;
		std::string Depth;

		PhysicsProbeReadingStrings(
			std::string speed,
			std::string temperature,
			std::string depth)
			: Speed(speed)
			, Temperature(temperature)
			, Depth(depth)
		{}
	};

	std::optional<PhysicsProbeReadingStrings> mPhysicsProbeReadingStrings;
	bool mArePhysicsProbeReadingStringsDirty;

	//
	// Units system
	//

	UnitsSystem mDisplayUnitsSystem;
	// No need to track dirtyness

	//
	// Interactions
	//

	struct HeatBlasterInfo
	{
		vec2f WorldCoordinates;
		float Radius;
		HeatBlasterActionType Action;

		HeatBlasterInfo(
			vec2f const & worldCoordinates,
			float radius,
			HeatBlasterActionType action)
			: WorldCoordinates(worldCoordinates)
			, Radius(radius)
			, Action(action)
		{}
	};

	// When set, will be uploaded to display the HeatBlaster flame
	// - and then reset (one-time use, it's a special case as it's really UI)
	std::optional<HeatBlasterInfo> mHeatBlasterFlameToRender;

	struct FireExtinguisherSpray
	{
		vec2f WorldCoordinates;
		float Radius;

		FireExtinguisherSpray(
			vec2f const & worldCoordinates,
			float radius)
			: WorldCoordinates(worldCoordinates)
			, Radius(radius)
		{}
	};

	// When set, will be uploaded to display the fire extinguisher spray
	// - and then reset (one-time use, it's a special case as it's really UI)
	std::optional<FireExtinguisherSpray> mFireExtinguisherSprayToRender;

	struct BlastToolHalo
	{
		vec2f WorldCoordinates;
		float Radius;
		float RenderProgress;
		float PersonalitySeed;

		BlastToolHalo(
			vec2f const & worldCoordinates,
			float radius,
			float renderProgress,
			float personalitySeed)
			: WorldCoordinates(worldCoordinates)
			, Radius(radius)
			, RenderProgress(renderProgress)
			, PersonalitySeed(personalitySeed)
		{}
	};

	// When set, will be uploaded to display the blast
	// - and then reset (one-time use, it's a special case as it's really UI)
	std::optional<BlastToolHalo> mBlastToolHaloToRender;
};
