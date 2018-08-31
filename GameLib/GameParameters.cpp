/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2018-04-13
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "GameParameters.h"

GameParameters::GameParameters()
    : StiffnessAdjustment(1.0f)
    , StrengthAdjustment(1.0f)
    , BuoyancyAdjustment(1.0f)
    , WaterVelocityDrag(0.015625f)
    , WaterVelocityAdjustment(1.0f)
    , WaveHeight(2.5f)
    , SeaDepth(200.0f)
    , DestroyRadius(0.75f)
    , BombBlastRadius(2.5f)
    , BombMass(5000.0f)
    , TimerBombInterval(10)
    , ToolSearchRadius(2.0f)
    , LightDiffusionAdjustment(0.5f)
    , NumberOfClouds(50)
    , WindSpeed(3.0f)
    , IsUltraViolentMode(false)
{
}
