/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2019-10-12
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "SoundController.h"

#include <Game/IGameControllerSettings.h>

#include <GameCore/Settings.h>

enum class GameSettings : size_t
{
    NumMechanicalDynamicsIterationsAdjustment = 0,
    SpringStiffnessAdjustment,
    SpringDampingAdjustment,
    SpringStrengthAdjustment,
    GlobalDampingAdjustment,
    RotAcceler8r,
    StaticPressureForceAdjustment,

    // Air
    AirDensityAdjustment,
    AirFrictionDragAdjustment,
    AirPressureDragAdjustment,

    // Water
    WaterDensityAdjustment,
    WaterFrictionDragAdjustment,
    WaterPressureDragAdjustment,
    WaterImpactForceAdjustment,
    HydrostaticPressureCounterbalanceAdjustment,
    WaterIntakeAdjustment,
    WaterDiffusionSpeedAdjustment,
    WaterCrazyness,
    DoDisplaceWater,
    WaterDisplacementWaveHeightAdjustment,

    // Waves
    BasalWaveHeightAdjustment,
    BasalWaveLengthAdjustment,
    BasalWaveSpeedAdjustment,
    TsunamiRate,
    RogueWaveRate,
    DoModulateWind,
    WindSpeedBase,
    WindSpeedMaxFactor,
    WaveSmoothnessAdjustment,

    // Storm
    StormRate,
    StormDuration,
    StormStrengthAdjustment,
    DoRainWithStorm,
    RainFloodAdjustment,
    LightningBlastProbability,

    // Heat
    AirTemperature,
    WaterTemperature,
    MaxBurningParticles,
    ThermalConductivityAdjustment,
    HeatDissipationAdjustment,
    IgnitionTemperatureAdjustment,
    MeltingTemperatureAdjustment,
    CombustionSpeedAdjustment,
    CombustionHeatAdjustment,
    HeatBlasterHeatFlow,
    HeatBlasterRadius,
    LaserRayHeatFlow,

    // Electricals
    LuminiscenceAdjustment,
    LightSpreadAdjustment,
    ElectricalElementHeatProducedAdjustment,
    EngineThrustAdjustment,
    WaterPumpPowerAdjustment,

    // Fishes
    NumberOfFishes,
    FishSizeMultiplier,
    FishSpeedAdjustment,
    DoFishShoaling,
    FishShoalRadiusAdjustment,

    // Misc
    OceanFloorTerrain,
    SeaDepth,
    OceanFloorBumpiness,
    OceanFloorDetailAmplification,
    OceanFloorElasticity,
    OceanFloorFriction,
    OceanFloorSiltHardness,
    DestroyRadius,
    RepairRadius,
    RepairSpeedAdjustment,
    BombBlastRadius,
    BombBlastForceAdjustment,
    BombBlastHeat,
    AntiMatterBombImplosionStrength,
    FloodRadius,
    FloodQuantity,
    InjectPressureQuantity,
    BlastToolRadius,
    BlastToolForceAdjustment,
    ScrubRotToolRadius,
    WindMakerToolWindSpeed,
    UltraViolentMode,
    DoGenerateDebris,
    SmokeEmissionDensityAdjustment,
    SmokeParticleLifetimeAdjustment,
    DoGenerateSparklesForCuts,
    AirBubblesDensity,
    DoGenerateEngineWakeParticles,
    NumberOfStars,
    NumberOfClouds,
    DoDayLightCycle,
    DayLightCycleDuration,
    ShipStrengthRandomizationDensityAdjustment,
    ShipStrengthRandomizationExtent,

    // Render
    FlatSkyColor,
    OceanTransparency,
    OceanDarkeningRate,
    ShipAmbientLightSensitivity,
    FlatLampLightColor,
    DefaultWaterColor,
    WaterContrast,
    WaterLevelOfDetail,
    ShowShipThroughOcean,
    DebugShipRenderMode,
    OceanRenderMode,
    TextureOceanTextureIndex,
    DepthOceanColorStart,
    DepthOceanColorEnd,
    FlatOceanColor,
    OceanRenderDetail,
    LandRenderMode,
    TextureLandTextureIndex,
    FlatLandColor,
    VectorFieldRenderMode,
    ShowShipStress,
    ShowShipFrontiers,
    ShowAABBs,
    HeatRenderMode,
    HeatSensitivity,
    StressRenderMode,
    DrawExplosions,
    DrawFlames,
    ShipFlameSizeAdjustment,
    DrawHeatBlasterFlame,

    // Sound
    MasterEffectsVolume,
    MasterToolsVolume,
    PlayBreakSounds,
    PlayStressSounds,
    PlayWindSound,
    PlayAirBubbleSurfaceSound,

    _Last = PlayAirBubbleSurfaceSound
};

class SettingsManager final : public BaseSettingsManager<GameSettings>
{
public:

    SettingsManager(
        IGameControllerSettings & gameControllerSettings,
        SoundController & soundController,
        std::filesystem::path const & rootSystemSettingsDirectoryPath,
        std::filesystem::path const & rootUserSettingsDirectoryPath);

private:

    static BaseSettingsManagerFactory MakeSettingsFactory(
        IGameControllerSettings & gameControllerSettings,
        SoundController & soundController);
};